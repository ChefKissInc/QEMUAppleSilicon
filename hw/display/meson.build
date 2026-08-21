hw_display_modules = {}

system_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files('apple_displaypipe_v4.c', 'apple_displaypipe_v2.c', 'synopsys_mipi_dsim.c', 'apple_scaler.c'))
system_ss.add(when: 'CONFIG_DDC', if_true: files('i2c-ddc.c'))
system_ss.add(when: 'CONFIG_EDID', if_true: files('edid-generate.c', 'edid-region.c'))

system_ss.add(when: 'CONFIG_FRAMEBUFFER', if_true: files('framebuffer.c'))

system_ss.add(when: 'CONFIG_DPCD', if_true: files('dpcd.c'))

modules += { 'hw-display': hw_display_modules }
