/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * reference: 
 *   amixer contents; amixer controls;
 *   http://www.tldp.org/HOWTO/Alsa-sound-6.html 
 */

#ifndef UtilsCommon_H
#define UtilsCommon_H

#include <json-c/json.h>
#include <afb/afb-binding.h>
#include <afb/afb-service-itf.h>

#include <alsa/asoundlib.h>


#ifndef PUBLIC
  #define PUBLIC
#endif
#define STATIC static


typedef struct {
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
} hwparamsT;

PUBLIC void pingtest(struct afb_req request);
PUBLIC ssize_t test_wavefile(struct afb_req request, int fd, hwparamsT *hwparams, u_char *_buffer, size_t size);
PUBLIC ssize_t safe_read(int fd, void *buf, size_t count);

#endif /* UtilsCommon_H */

