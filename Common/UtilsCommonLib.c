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
 */
#define _GNU_SOURCE  // needed for vasprintf

#include <json-c/json.h>
#include <afb/afb-binding.h>
#include <afb/afb-service-itf.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>


#include "UtilsCommonLib.h"
#include "AudioFormat.h"
#include <unistd.h>

#define DEFAULT_FORMAT		SND_PCM_FORMAT_U8

// hoops this should be removed
#ifndef LLONG_MAX
#define LLONG_MAX    9223372036854775807LL
#endif
static off64_t pbrec_count = LLONG_MAX; 

#define check_wavefile_space(request, buffer, len, blimit) \
	if (len > blimit) { \
		blimit = len; \
		if ((buffer = realloc(buffer, blimit)) == NULL) { \
			afb_req_fail_f (request, "check_wavefile_space-malloc", "check_wavefile_space Not Enough Memory line=%d", __LINE__);  \
			goto OnErrorExit;  \
		} \
	}

// Extracted from Alsautils/aplay.c
PUBLIC ssize_t safe_read(int fd, void *buf, size_t count) {
	ssize_t result = 0, res;

	while (count > 0) {
		if ((res = read(fd, buf, count)) == 0)
			break;
		if (res < 0)
			return result > 0 ? result : res;
		count -= res;
		result += res;
		buf = (char *)buf + res;
	}
	return result;
}

// Extracted from Alsautils/aplay.c
static size_t test_wavefile_read(struct afb_req request, int fd, u_char *buffer, size_t *size, size_t reqsize, int line) {
	if (*size >= reqsize)
		return *size;
	if ((size_t)safe_read(fd, buffer + *size, reqsize - *size) != reqsize - *size) {
		afb_req_fail_f (request, "read error (called from %s/%d)", __FILE__, line);
		return 0;
	}
	return *size = reqsize;
}

// Extracted from Alsautils/aplay.c
PUBLIC ssize_t test_wavefile(struct afb_req request, int fd, hwparamsT *hwparams, u_char *_buffer, size_t size) {
	WaveHeader *h = (WaveHeader *)_buffer;
	u_char *buffer = NULL;
	size_t blimit = 0;
	WaveFmtBody *f;
	WaveChunkHeader *c;
	u_int type, len;
	unsigned short format, channels;
	int big_endian, native_format;

	if (size < sizeof(WaveHeader))
		return -1;
	if (h->magic == WAV_RIFF)
		big_endian = 0;
	else if (h->magic == WAV_RIFX)
		big_endian = 1;
	else
		return -1;
	if (h->type != WAV_WAVE)
		return -1;

	if (size > sizeof(WaveHeader)) {
		check_wavefile_space(request, buffer, size - sizeof(WaveHeader), blimit);
		memcpy(buffer, _buffer + sizeof(WaveHeader), size - sizeof(WaveHeader));
	}
	size -= sizeof(WaveHeader);
	while (1) {
		check_wavefile_space(request, buffer, sizeof(WaveChunkHeader), blimit);
		test_wavefile_read(request, fd, buffer, &size, sizeof(WaveChunkHeader), __LINE__);
		c = (WaveChunkHeader*)buffer;
		type = c->type;
		len = TO_CPU_INT(c->length, big_endian);
		len += len % 2;
		if (size > sizeof(WaveChunkHeader))
			memmove(buffer, buffer + sizeof(WaveChunkHeader), size - sizeof(WaveChunkHeader));
		size -= sizeof(WaveChunkHeader);
		if (type == WAV_FMT)
			break;
		check_wavefile_space(request, buffer, len, blimit);
		test_wavefile_read(request, fd, buffer, &size, len, __LINE__);
		if (size > len)
			memmove(buffer, buffer + len, size - len);
		size -= len;
	}

	if (len < sizeof(WaveFmtBody)) {
		afb_req_fail_f (request, "wavefile:header", "unknown length of 'fmt ' chunk (read %u, should be %u at least)", len, (u_int)sizeof(WaveFmtBody));
		goto OnErrorExit;
	}
	check_wavefile_space(request, buffer, len, blimit);
	test_wavefile_read(request, fd, buffer, &size, len, __LINE__);
	f = (WaveFmtBody*) buffer;
	format = TO_CPU_SHORT(f->format, big_endian);
	if (format == WAV_FMT_EXTENSIBLE) {
		WaveFmtExtensibleBody *fe = (WaveFmtExtensibleBody*)buffer;
		if (len < sizeof(WaveFmtExtensibleBody)) {
			afb_req_fail_f (request,"wavefile:header", "unknown length of extensible 'fmt ' chunk (read %u, should be %u at least)",len, (u_int)sizeof(WaveFmtExtensibleBody));
        		goto OnErrorExit;
		}
		if (memcmp(fe->guid_tag, WAV_GUID_TAG, 14) != 0) {
			afb_req_fail_f (request,"wavefile:header","wrong format tag in extensible 'fmt ' chunk");
        		goto OnErrorExit;
		}
		format = TO_CPU_SHORT(fe->guid_format, big_endian);
	}
	if (format != WAV_FMT_PCM &&
	    format != WAV_FMT_IEEE_FLOAT) {
                afb_req_fail_f (request,"wavefile:header", "can't play WAVE-file format 0x%04x which is not PCM or FLOAT encoded", format);
		goto OnErrorExit;
	}
	channels = TO_CPU_SHORT(f->channels, big_endian);
	if (channels < 1) {
		afb_req_fail_f (request, "wavefile:header","can't play WAVE-files with %d tracks", channels);
		goto OnErrorExit;
	}
	hwparams->channels = channels;
	switch (TO_CPU_SHORT(f->bit_p_spl, big_endian)) {
	case 8:
		if (hwparams->format != DEFAULT_FORMAT && hwparams->format != SND_PCM_FORMAT_U8)
	    	    fprintf(stderr, "Warning: format is changed to U8\n");
		hwparams->format = SND_PCM_FORMAT_U8;
		break;
	case 16:
		if (big_endian)
			native_format = SND_PCM_FORMAT_S16_BE;
		else
			native_format = SND_PCM_FORMAT_S16_LE;
		if (hwparams->format != DEFAULT_FORMAT &&  hwparams->format != native_format)
                    fprintf(stderr, "Warning: format is changed to %s\n", snd_pcm_format_name(native_format));
		hwparams->format = native_format;
		break;
	case 24:
		switch (TO_CPU_SHORT(f->byte_p_spl, big_endian) / hwparams->channels) {
		case 3:
			if (big_endian)
				native_format = SND_PCM_FORMAT_S24_3BE;
			else
				native_format = SND_PCM_FORMAT_S24_3LE;
			if (hwparams->format != DEFAULT_FORMAT && hwparams->format != native_format)
				fprintf(stderr, "Warning: format is changed to %s\n", snd_pcm_format_name(native_format));
			hwparams->format = native_format;
			break;
		case 4:
			if (big_endian)
				native_format = SND_PCM_FORMAT_S24_BE;
			else
				native_format = SND_PCM_FORMAT_S24_LE;
			if (hwparams->format != DEFAULT_FORMAT &&
			    hwparams->format != native_format)
				fprintf(stderr, "Warning: format is changed to %s\n", snd_pcm_format_name(native_format));
			hwparams->format = native_format;
			break;
		default:
			afb_req_fail_f (request,"wav-header", "can't play WAVE-files with sample %d bits in %d bytes wide (%d channels)"
                               , TO_CPU_SHORT(f->bit_p_spl, big_endian), TO_CPU_SHORT(f->byte_p_spl, big_endian),  hwparams->channels);
                            goto OnErrorExit;
		}
		break;
	case 32:
		if (format == WAV_FMT_PCM) {
			if (big_endian)
				native_format = SND_PCM_FORMAT_S32_BE;
			else
				native_format = SND_PCM_FORMAT_S32_LE;
                        hwparams->format = native_format;
		} else if (format == WAV_FMT_IEEE_FLOAT) {
			if (big_endian)
				native_format = SND_PCM_FORMAT_FLOAT_BE;
			else
				native_format = SND_PCM_FORMAT_FLOAT_LE;
			hwparams->format = native_format;
		}
		break;
	default:
		afb_req_fail_f (request,"wav-header",  "can't play WAVE-files with sample %d bits wide", TO_CPU_SHORT(f->bit_p_spl, big_endian));
		goto OnErrorExit;
	}
	hwparams->rate = TO_CPU_INT(f->sample_fq, big_endian);
	
	if (size > len)
		memmove(buffer, buffer + len, size - len);
	size -= len;
	
	while (1) {
		u_int type, len;

		check_wavefile_space(request, buffer, sizeof(WaveChunkHeader), blimit);
		test_wavefile_read(request, fd, buffer, &size, sizeof(WaveChunkHeader), __LINE__);
		c = (WaveChunkHeader*)buffer;
		type = c->type;
		len = TO_CPU_INT(c->length, big_endian);
		if (size > sizeof(WaveChunkHeader))
			memmove(buffer, buffer + sizeof(WaveChunkHeader), size - sizeof(WaveChunkHeader));
		size -= sizeof(WaveChunkHeader);
		if (type == WAV_DATA) {
			if (len < pbrec_count && len < 0x7ffffffe)
				pbrec_count = len;
			if (size > 0)
				memcpy(_buffer, buffer, size);
			free(buffer);
			return size;
		}
		len += len % 2;
		check_wavefile_space(request, buffer, len, blimit);
		test_wavefile_read(request, fd, buffer, &size, len, __LINE__);
		if (size > len)
			memmove(buffer, buffer + len, size - len);
		size -= len;
	}

        afb_req_fail_f (request, "wavheader-hoop", "Should never be here file=%s line=%s", __FILE__, __LINE__);

OnErrorExit:            
	return -1;
}

PUBLIC int cbCheckResponse(struct afb_req request, int iserror, struct json_object *result) {
    struct json_object *response, *status, *info;

    if (iserror) { // on error proxy information we got from lower layer
        if (result) {
            if (json_object_object_get_ex(result, "request", &response)) {
                json_object_object_get_ex(response, "info", &info);
                json_object_object_get_ex(response, "status", &status);
                afb_req_fail(request, json_object_get_string(status), json_object_get_string(info));
                goto OnErrorExit;
            }
        } else {
            afb_req_fail(request, "cbCheckFail", "No Result inside API response");
        }
        goto OnErrorExit;
    }
    return (0);

OnErrorExit:
    return (-1);
}


// This function should be part of Generic AGL Framework
PUBLIC json_object* afb_service_call_sync(struct afb_service srvitf, struct afb_req request, char* api, char* verb, struct json_object* queryurl, void *handle) {
    json_object* response = NULL;
    int status = 0;
    sem_t semid;

    // Nested procedure are allow in GNU and allow us to keep caller stack valid

    void callback(void *handle, int iserror, struct json_object * result) {

        // Process Basic Error
        if (!cbCheckResponse(request, iserror, result)) {
            status = -1;
            goto OnExitCB;
        }

        // Get response from object
        json_object_object_get_ex(result, "response", &response);
        if (!response) {
            afb_req_fail_f(request, "response-notfound", "No Controls return from alsa/getcontrol result=[%s]", json_object_get_string(result));
            goto OnExitCB;
        }

OnExitCB:
        sem_post(&semid);
    }

    // Create an exclusive semaphore
    status = sem_init(&semid, 0, 0);
    if (status < 0) {
        afb_req_fail_f(request, "error:seminit", "Fail to allocate semaphore err=[%s]", strerror(status));
        goto OnExit;
    }

    // Call service and wait for call back to finish before moving any further
    afb_service_call(srvitf, "alsacore", "getctl", queryurl, callback, handle);
    sem_wait(&semid);

OnExit:
    return (response);
}

PUBLIC void pingtest(struct afb_req request) {
    json_object *query = afb_req_json(request);
    afb_req_success(request, query, NULL);
}
