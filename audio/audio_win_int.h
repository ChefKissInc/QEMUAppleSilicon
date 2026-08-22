#pragma once

int waveformat_from_audio_settings (WAVEFORMATEX *wfx,
                                    struct audsettings *as);

int waveformat_to_audio_settings (WAVEFORMATEX *wfx,
                                  struct audsettings *as);
