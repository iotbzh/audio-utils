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
 * references:
 *   https://gist.github.com/ghedo/963382
 *   http://alsa-utils.sourcearchive.com/documentation/1.0.15/aplay_8c-source.html
 */

#define _GNU_SOURCE

#define BUFFER_FRAME_COUNT 10 // max frames in buffer
#define WAIT_TIMER_US 1000000 // default waiting timer 1s

#include "PlayerBinding.h"
#include "AudioFormat.h"
#include <systemd/sd-event.h>
 #include <sys/ioctl.h>


typedef struct {
    long count;
    long byteIn;
    long byteOut;
    long frameCt;
    snd_pcm_uframes_t frameSz;
    snd_pcm_t *pcmHandle;
    sd_event_source *pollFileSrc;
    char *buffer;
    int fileFd;
} aplayHandleT;

STATIC int tryWritePcmCB (sd_event_source* src, uint64_t timer, void* handle) {
    aplayHandleT *aplayHandle = (aplayHandleT*) handle;
    long count;
    
    // Push date on PCM return number of pushed frame
    while (aplayHandle->frameCt > 0) {
        count = snd_pcm_writei(aplayHandle->pcmHandle, (const void*)&aplayHandle->buffer[aplayHandle->byteOut], aplayHandle->frameSz);
        
        // PCM buffer is full set a timer
        if (-EPIPE) {
            sd_event_add_time(afb_daemon_get_event_loop(afbIface->daemon), NULL, CLOCK_MONOTONIC, WAIT_TIMER_US, 0, tryWritePcmCB, handle);
            return 0;
        }

        // update byteOut pointer
        aplayHandle->frameCt --;
        aplayHandle->byteOut = aplayHandle->byteOut + (count * (int) aplayHandle->frameSz);
        
        // reach the end of buffer let's loop
        if ((aplayHandle->byteOut / aplayHandle->frameSz) >=  BUFFER_FRAME_COUNT) aplayHandle->byteOut = 0;    
    }
    
    // This should be improve to handle under run
    if (aplayHandle->frameCt == 0) {
        snd_pcm_close (aplayHandle->pcmHandle);
        DEBUG (afbIface, "Closing PCM");
    }
    
    return 0;    
}

STATIC int tryReadFileCB(sd_event_source* src,uint64_t timer, void* handle) {
    aplayHandleT *aplayHandle = (aplayHandleT*) handle;
    // if not enough space for at least one frame then loop 
    if ((aplayHandle->byteIn / aplayHandle->frameSz) >= BUFFER_FRAME_COUNT) aplayHandle->byteIn = 0;
    
    // compute max number of buffer space for new frames
    long frame2read;
    long write2read = (aplayHandle->byteOut-aplayHandle->byteIn)/(int)aplayHandle->frameSz;
    long read2end   = BUFFER_FRAME_COUNT*(int)aplayHandle->frameSz-(aplayHandle->byteIn/(int)aplayHandle->frameSz)-1;
    
    if (write2read > 0 && write2read < read2end) frame2read=write2read;
    else frame2read = read2end;
    
    // try reading some data
    long count = read (aplayHandle->fileFd, &aplayHandle->buffer[aplayHandle->byteIn], aplayHandle->frameSz*frame2read);
    DEBUG (afbIface, "readbyte=%ld writebyte=%ld, freeframe=%ld, received=%ld size=%d", aplayHandle->byteIn, aplayHandle->byteOut, frame2read, count/(long)aplayHandle->frameSz, BUFFER_FRAME_COUNT);
    
    if (count < 0) {
        if (count||EWOULDBLOCK > 0) {
            // retry in approximatively one second
            sd_event_add_time(afb_daemon_get_event_loop(afbIface->daemon), NULL, CLOCK_MONOTONIC, WAIT_TIMER_US, 0, tryReadFileCB, handle);
           // activate some timer            
        } else {
          // file reach EOF 
          (void)sd_event_source_unref(aplayHandle->pollFileSrc);
          close (aplayHandle->fileFd);
        }
        return 0;
    }
    
    // update frame waiting count & buffer index
    aplayHandle->frameCt = aplayHandle->frameCt + count / aplayHandle->frameSz;
    aplayHandle->byteIn  = aplayHandle->byteIn + count;
    
    // we have waiting data try forcing a write
    if (aplayHandle->frameCt) tryWritePcmCB (NULL, 0, handle);   

    return 0;    
}

STATIC  int asyncReadCB (sd_event_source* src, int fileFd, uint32_t revents, void* handle) {
    aplayHandleT *aplayHandle = (aplayHandleT*) handle;
    
    if ((revents & EPOLLHUP) != 0) {
        NOTICE (afbIface, "asyncReadCB hanghup [file closed ?");
        (void)sd_event_source_unref(aplayHandle->pollFileSrc);
        close (aplayHandle->fileFd);
        goto ExitOnSucess;
    }
    
    // if buffer already full just wait 1s
    if (aplayHandle->frameCt == BUFFER_FRAME_COUNT) {
        sd_event_add_time(afb_daemon_get_event_loop(afbIface->daemon), NULL, CLOCK_MONOTONIC, WAIT_TIMER_US, 0, tryReadFileCB, handle);
    }
    
    // Data are waiting let's try to read them    
    if ((revents & EPOLLIN) != 0) tryReadFileCB (NULL, 0, handle);
    
    ExitOnSucess:
        return 0;
    
}

// Delegate to lowerlevel the mapping of soundcard name with soundcard ID
PUBLIC void playTest(struct afb_req request) {
    int err, timeout=0, fileFd;
    unsigned int channels, rate, period;
    snd_pcm_hw_params_t *hwparams;
    hwparamsT wavParams;
    aplayHandleT *aplayHandle=NULL;
    u_char *audiobuf;

    
    char *pcmname="plug:music";

    const char *tmp = afb_req_value(request, "timeout");
    if (!tmp) {
        err = sscanf(tmp, "%d", &timeout);
        if (err != 1 || timeout < 0) {
            afb_req_fail_f (request, "timeout-invalid", "Timeout should be a positive integer in second Timeout=[%s]", tmp);
            goto OnErrorExit;            
        }
    }
    
    const char *filename = afb_req_value(request, "filename");
    if (!filename) {
        afb_req_fail_f (request, "filename-missing", "No filename given");
        goto OnErrorExit;            
    }
    
    if ((fileFd = open(filename, O_RDONLY, 0)) == -1) {
        afb_req_fail_f (request, "infile-open", "Fail to open filename=%s err=%s", filename, strerror(fileFd));
        goto OnErrorExit;            
    }

    /* read the WAV file header Alsa aplay method */
    audiobuf=malloc(1024);
    size_t dta = sizeof(AuHeader);
    if ((size_t)safe_read(fileFd, audiobuf, dta) != dta) {
        afb_req_fail_f (request, "infile-header", "Fail to read file WAV header file Filename=[%s]", filename);
        goto OnErrorExit;            
    }

    ssize_t dtawave = test_wavefile(request, fileFd, &wavParams, audiobuf, dta);
    if (dtawave <0) {
        afb_req_fail_f (request, "infile-wav", "Fail to read file WAV header file Filename=[%s]", tmp);
        goto OnErrorExit;            
    }
    free(audiobuf);

    err = snd_pcm_open(&aplayHandle->pcmHandle, pcmname, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err) {
        afb_req_fail_f (request, "pcm-open", "fail to open PCM=[%s] Err=%s",  pcmname, snd_strerror(err));
        goto OnErrorExit;
    }
    
    // Load target PCM default config
    aplayHandle= alloca (sizeof(aplayHandleT));
    snd_pcm_hw_params_alloca(&hwparams);
    snd_pcm_hw_params_any(aplayHandle->pcmHandle, hwparams); 
  
    err = snd_pcm_hw_params_set_access(aplayHandle->pcmHandle, hwparams,SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err) {
        afb_req_fail_f (request, "pcm-interleave", "fail to set Interleave mode PCM=[%s] Err=%s",  pcmname, snd_strerror(err));
        goto OnErrorExit;
    }

    err = snd_pcm_hw_params_set_format(aplayHandle->pcmHandle, hwparams, wavParams.format);
    if (err) {
        afb_req_fail_f (request, "pcm-format", "fail to set format=%d PCM=[%s] Err=%s",  wavParams.format, pcmname, snd_strerror(err));
        goto OnErrorExit;
    }
    
    err = snd_pcm_hw_params_set_channels(aplayHandle->pcmHandle, hwparams, wavParams.channels);
    if (err) {
        afb_req_fail_f (request, "pcm-format", "fail to set channels=%d PCM=[%s] Err=%s",  wavParams.format, wavParams.channels, snd_strerror(err));
        goto OnErrorExit;
    }
    
    err = snd_pcm_hw_params_set_rate_near (aplayHandle->pcmHandle, hwparams, &wavParams.rate, 0);
    if (err) {
        afb_req_fail_f (request, "pcm-format", "fail to set rate=%d PCM=[%s] Err=%s",  wavParams.format, wavParams.rate, snd_strerror(err));
        goto OnErrorExit;
    }
    
    err= snd_pcm_hw_params(aplayHandle->pcmHandle, hwparams);
    if (err) {
        afb_req_fail_f (request, "pcm-hwparams", "fail to write config hwparams PCM=[%s] Err=%s",  pcmname, snd_strerror(err));
        goto OnErrorExit;
    }
    
    // some debug info to make sure we're on track
    snd_pcm_hw_params_get_channels(hwparams, &channels);
    snd_pcm_hw_params_get_rate(hwparams, &rate, 0);
    DEBUG (afbIface, "PCM name=[%s] STATE=[%d] Channel=%d Rate=%d",  snd_pcm_name(aplayHandle->pcmHandle), snd_pcm_state(aplayHandle->pcmHandle), channels, rate);

    // allocate buffer to hold single period 
    err= snd_pcm_hw_params_get_period_size(hwparams, &aplayHandle->frameSz, 0);
    aplayHandle->byteIn  = 0;
    aplayHandle->byteOut = 0;
    aplayHandle->frameCt  = 0;
    aplayHandle->fileFd = fileFd;
    aplayHandle->buffer = alloca(aplayHandle->frameSz * BUFFER_FRAME_COUNT);
    
    err=snd_pcm_hw_params_get_period_time(hwparams, &period, NULL);
    if (err) {
        afb_req_fail_f (request, "pcm-period", "fail to retrieve period time PCM=[%s] Err=%s",  pcmname, snd_strerror(err));
        goto OnErrorExit;
    }
    
    // if a timeout was given let's translate it into period count
    if (timeout != 0) aplayHandle->count = timeout * WAIT_TIMER_US / period;

    // cleanup PCM buffer
    snd_pcm_nonblock(aplayHandle->pcmHandle, 0);
    snd_pcm_drain(aplayHandle->pcmHandle);
    
    // opening PCM in non blocmode is not enough
    err = snd_pcm_nonblock(aplayHandle->pcmHandle, 1);
    if (err < 0) {
        afb_req_fail_f (request, "pcm-nobloc", "fail to select nonblock mode PCM=[%s] Err=%s",  pcmname, snd_strerror(err));
        goto OnErrorExit;
    }
    
    // set infile fd for asynchronous operation
    err = ioctl (aplayHandle->fileFd, F_SETFL, O_ASYNC);
    if (err <= 0) {
        afb_req_fail_f (request, "file-async", "fail to set async mode to read file Filename=[%s] Err=%s",  filename, snd_strerror(err));
        goto OnErrorExit;        
    }
       
    // register aplayHandle file fd into binder mainloop
    err = sd_event_add_io(afb_daemon_get_event_loop(afbIface->daemon), &aplayHandle->pollFileSrc, fileFd, EPOLLIN, asyncReadCB, &aplayHandle);
    if (err < 0) {
        afb_req_fail_f (request, "register-mainloop", "Cannot hook events to mainloop");
        goto OnErrorExit;
    }

   OnErrorExit: 
    if (aplayHandle) {
        snd_pcm_drain(aplayHandle->pcmHandle);
	snd_pcm_close(aplayHandle->pcmHandle);
    }
}
