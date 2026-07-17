// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023 Rene Wolf

#include "dbg.h"
#include "pcm1802.h"
#include "pcm1802_fmt00.pio.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "usb_audio_format.h"
#include "wdt_trace.h"

// see also https://www.pjrc.com/pcm1802-breakout-board-needs-hack/
#define PCM1802_POWER_DOWN_PIN 17


// NOTE GPIOs must be consecutive for the PIO to work and in the order of DATA, BITCLK, LRCLK
#define PCM_PIO_ADC0_DATA   18
#define PCM_PIO_ADC0_BITCLK 19
#define PCM_PIO_ADC0_LRCLK  20
// not connected / outputs debug info from PIO
#define PCM_PIO_ADC0_DEBUG  21

static_assert((PCM_PIO_ADC0_DATA + pcm1802_index_data)   == PCM_PIO_ADC0_DATA,   "ADC0 DATA GPIO not where it should be");
static_assert((PCM_PIO_ADC0_DATA + pcm1802_index_bitclk) == PCM_PIO_ADC0_BITCLK, "ADC0 BITCLK GPIO not where it should be");
static_assert((PCM_PIO_ADC0_DATA + pcm1802_index_lrclk)  == PCM_PIO_ADC0_LRCLK,  "ADC0 LRCLK GPIO not where it should be");
static_assert((PCM_PIO_ADC0_DATA + pcm1802_index_dbg)    == PCM_PIO_ADC0_DEBUG,  "ADC0 DEBUG GPIO not where it should be");




static PIO pio;
static uint32_t pio_program_offset;
static uint32_t pio_sm;
uint32_t pcm1802_out_of_sync_drops;
uint32_t pcm1802_rch_tmo_count;
uint32_t pcm1802_rch_tmo_value;

// The PIO RX FIFO is drained by DMA into a ping-pong buffer pair, one word per
// channel sample (bit 24 tags right-channel words, see the .pio file). 128 words
// = 64 L/R frames, about 1.4 ms per half at 46875 Hz. Even sized halves mean L/R
// pairs only straddle a half boundary after a mid-stream drop.
#define PCM_DMA_WORDS_PER_HALF 128
static uint32_t dma_buf[2][PCM_DMA_WORDS_PER_HALF];
static int dma_ch[2];
// completed halves, written by the DMA IRQ on core1, read by the consumer on core1
static volatile uint32_t dma_halves_produced;
// fully consumed halves + read position inside the current one (consumer only)
static uint32_t dma_halves_consumed;
static uint32_t dma_word_idx;
// a left-channel word waiting for its right partner from the next half
static bool have_pending_l;
static uint32_t pending_l;

static void dma_irq1_handler()
{
	for(int i=0; i<2; ++i)
	{
		if( dma_channel_get_irq1_status(dma_ch[i]) )
		{
			dma_channel_acknowledge_irq1(dma_ch[i]);
			// re-arm this half for its next turn, the chain to the other channel has already fired
			dma_channel_set_trans_count(dma_ch[i], PCM_DMA_WORDS_PER_HALF, false);
			dma_channel_set_write_addr(dma_ch[i], dma_buf[i], false);
			++dma_halves_produced;
		}
	}
}

static void pcm_dma_init()
{
	dma_ch[0] = dma_claim_unused_channel(true);
	dma_ch[1] = dma_claim_unused_channel(true);

	for(int i=0; i<2; ++i)
	{
		dma_channel_config cfg = dma_channel_get_default_config(dma_ch[i]);
		channel_config_set_read_increment(&cfg, false);
		channel_config_set_write_increment(&cfg, true);
		channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
		channel_config_set_dreq(&cfg, pio_get_dreq(pio, pio_sm, false));
		channel_config_set_chain_to(&cfg, dma_ch[1-i]);
		dma_channel_configure(dma_ch[i], &cfg, dma_buf[i], &pio->rxf[pio_sm], PCM_DMA_WORDS_PER_HALF, false);
		dma_channel_set_irq1_enabled(dma_ch[i], true);
	}

	// NOTE called from core1 (pcm1802_init), so the IRQ fires on core1 and all
	// producer/consumer state stays on one core
	irq_set_exclusive_handler(DMA_IRQ_1, dma_irq1_handler);
	irq_set_enabled(DMA_IRQ_1, true);
}

// Takes one channel word from the DMA ring if one is available
static bool try_take_word(uint32_t* out)
{
	uint32_t produced = dma_halves_produced;

	if( produced == dma_halves_consumed )
		return false;

	if( (produced - dma_halves_consumed) > 1 )
	{
		// the consumer fell behind and DMA started overwriting unread halves,
		// jump to the most recently completed one
		pcm1802_rch_tmo_count += 1;
		pcm1802_rch_tmo_value = produced - dma_halves_consumed;
		dma_halves_consumed = produced - 1;
		dma_word_idx = 0;
		have_pending_l = false;
	}

	*out = dma_buf[dma_halves_consumed & 1][dma_word_idx];
	++dma_word_idx;
	if( dma_word_idx == PCM_DMA_WORDS_PER_HALF )
	{
		dma_word_idx = 0;
		++dma_halves_consumed;
	}
	return true;
}

static uint32_t setup_pio(uint32_t pin)
{
	// https://github.com/raspberrypi/pico-examples/blob/a7ad17156bf60842ee55c8f86cd39e9cd7427c1d/pio/clocked_input/clocked_input.pio#L24
	// https://medium.com/geekculture/raspberry-pico-programming-with-pio-state-machines-e4610e6b0f29
	uint32_t sm = pio_claim_unused_sm(pio, true);

	pio_sm_config cfg = pcm1802_fmt00_program_get_default_config(pio_program_offset);


	// Set and initialize the input pins
	sm_config_set_in_pins(&cfg, pin);
	pio_sm_set_consecutive_pindirs(pio, sm, pin, (pcm1802_index_lrclk-pcm1802_index_data)+1, false);
	sm_config_set_jmp_pin(&cfg, pin + pcm1802_index_data);
	
	// Set and initialize the output pins
	sm_config_set_set_pins(&cfg, pin + pcm1802_index_dbg, 1);
	pio_sm_set_consecutive_pindirs(pio, sm, pin + pcm1802_index_dbg, 1, true);
	
	// we shift LEFT as we have MSB first on PCM interface
	sm_config_set_in_shift(&cfg, false, false, 32);
	
	// Connect these GPIOs to this PIO block
	pio_gpio_init(pio, pin + pcm1802_index_data);
	pio_gpio_init(pio, pin + pcm1802_index_bitclk);
	pio_gpio_init(pio, pin + pcm1802_index_lrclk);
	pio_gpio_init(pio, pin + pcm1802_index_dbg);

	// We only receive, so disable the TX FIFO to make the RX FIFO deeper.
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);

	// Load our configuration
	pio_sm_init(pio, sm, pio_program_offset, &cfg);
	
	return sm;
}

void pcm_pio_init()
{
	// https://github.com/raspberrypi/pico-examples/blob/a7ad17156bf60842ee55c8f86cd39e9cd7427c1d/pio/clocked_input/clocked_input.c#L45
	pio = pio0;
	pio_program_offset = pio_add_program(pio, &pcm1802_fmt00_program);

	pio_sm = setup_pio(PCM_PIO_ADC0_DATA);

	// the state machine stays disabled until pcm1802_start()
}

void pcm1802_init()
{
	gpio_init(PCM1802_POWER_DOWN_PIN);
	gpio_set_dir(PCM1802_POWER_DOWN_PIN, GPIO_OUT);
	gpio_put(PCM1802_POWER_DOWN_PIN, 0);
	pcm1802_out_of_sync_drops = 0;
	pcm1802_rch_tmo_count = 0;
	pcm1802_rch_tmo_value = 0;
	pcm_pio_init();
	pcm_dma_init();
}

void pcm1802_start()
{
	wdt_trace_core1(WDT_TRACE1_PCM_START);
	// re-enter the PIO program at its first instruction with clean FIFOs, so every
	// power-up acquires the L/R phase the same way a cold boot does
	pio_sm_set_enabled(pio, pio_sm, false);
	pio_sm_clear_fifos(pio, pio_sm);
	pio_sm_restart(pio, pio_sm);
	pio_sm_exec(pio, pio_sm, pio_encode_jmp(pio_program_offset));

	dma_halves_produced = 0;
	dma_halves_consumed = 0;
	dma_word_idx = 0;
	have_pending_l = false;
	for(int i=0; i<2; ++i)
	{
		dma_channel_set_trans_count(dma_ch[i], PCM_DMA_WORDS_PER_HALF, false);
		dma_channel_set_write_addr(dma_ch[i], dma_buf[i], false);
	}
	dma_channel_start(dma_ch[0]);

	pio_sm_set_enabled(pio, pio_sm, true);
	gpio_put(PCM1802_POWER_DOWN_PIN, 1);
	dbg_say("pcm1802_start\n");
	wdt_trace_core1(WDT_TRACE1_PCM_START_DONE);
}

void pcm1802_stop()
{
	wdt_trace_core1(WDT_TRACE1_PCM_STOP);
	gpio_put(PCM1802_POWER_DOWN_PIN, 0);
	// no DREQs once the state machine is off, so both channels stop cleanly
	pio_sm_set_enabled(pio, pio_sm, false);
	wdt_trace_core1(WDT_TRACE1_DMA_ABORT);
	dma_channel_abort(dma_ch[0]);
	dma_channel_abort(dma_ch[1]);
	dma_channel_acknowledge_irq1(dma_ch[0]);
	dma_channel_acknowledge_irq1(dma_ch[1]);
	pio_sm_clear_fifos(pio, pio_sm);
	dbg_say("pcm1802_stop\n");
	wdt_trace_core1(WDT_TRACE1_PCM_STOP_DONE);
}


void pcm1802_rx_24bit_uac_pcm_type1(uint8_t* l_3byte, uint8_t* r_3byte)
{
	while(pcm1802_try_rx_24bit_uac_pcm_type1(l_3byte, r_3byte) == false) { }
}

bool pcm1802_try_rx_24bit_uac_pcm_type1(uint8_t* l_3byte, uint8_t* r_3byte)
{
	while(true)
	{
		if( ! have_pending_l )
		{
			uint32_t word;
			if( ! try_take_word(&word) )
				return false;

			if( word & 0x01000000 )
			{
				// a right-channel word with no left partner -> out of sync, drop it
				++pcm1802_out_of_sync_drops;
				dbg_say("pcm1802 out of sync, drop!\n");
				return false;
			}

			pending_l = word;
			have_pending_l = true;
		}

		uint32_t word;
		if( ! try_take_word(&word) )
			return false; // right partner not captured yet, keep the pending left for the next call

		if( ! (word & 0x01000000) )
		{
			// two left words in a row -> the right one was lost, restart the pair from this word
			++pcm1802_out_of_sync_drops;
			dbg_say("pcm1802 out of sync, drop!\n");
			pending_l = word;
			continue;
		}

		usb_audio_pcm24_host_to_usb(l_3byte, pending_l);
		usb_audio_pcm24_host_to_usb(r_3byte, word);
		have_pending_l = false;
		return true;
	}
}

static bool wait_for_pos_edge_on_pin(uint32_t pin)
{
	// this takes long enough to not timeout on 46kHz which is our slowest clock line (LR clock)
	uint32_t tmo = 0xfff;
	
	while( gpio_get(pin) == true )
	{
		--tmo;
		if( tmo == 0)
			return false;
	}
	
	while( gpio_get(pin) == false )
	{
		--tmo;
		if( tmo == 0)
			return false;
	}
	
	return true;
}

bool pcm1802_activity_on_lrck()
{
	return wait_for_pos_edge_on_pin(PCM_PIO_ADC0_LRCLK);
}

bool pcm1802_activity_on_bck()
{
	return wait_for_pos_edge_on_pin(PCM_PIO_ADC0_BITCLK);
}

bool pcm1802_activity_on_data()
{
	return wait_for_pos_edge_on_pin(PCM_PIO_ADC0_DATA);
}
