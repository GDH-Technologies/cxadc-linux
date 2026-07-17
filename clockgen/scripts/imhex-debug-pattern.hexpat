// https://github.com/WerWolv/ImHex/wiki/Pattern-Language-Guide#endian-specification

struct debug_info {
	// should be 0x11223344
	le u32 magic;

	// true if the general startup of the si5351 clock generator chip looks fine
	u8 si5351_init_success;
	
	// true if there was activity on the PCM1802 lines, updated occasionally
	u8   pcm1802_activity_lrck;
	u8   pcm1802_activity_bck;
	u8   pcm1802_activity_data;

	// some specific counters on the PCM1802 subsystem
	le u32  pcm1802_out_of_sync_drops;
	le u32  pcm1802_rch_tmo_count;
	le u32  pcm1802_rch_tmo_value;

	// Counts RX timeout conditions in main1
	le u32 main1_rxsample_tmo;

	// true while the PCM1802 is powered and its PIO capture is running
	u8 adc_powered;
	// active alternate setting of the USB streaming interface (0 = closed, 1 = 2ch, 2 = 3ch)
	u8 usb_alt_setting;
	// number of times the ADC has been powered up since boot
	le u32 adc_power_cycles;
};
