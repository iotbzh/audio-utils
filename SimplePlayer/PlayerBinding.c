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
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>

#include "PlayerBinding.h"

PUBLIC const struct afb_binding_interface *afbIface;
PUBLIC struct afb_service afbSrv;

/*
 * array of the verbs exported to afb-daemon
 */
STATIC const struct afb_verb_desc_v1 binding_verbs[] = {
  /* VERB'S NAME            SESSION MANAGEMENT          FUNCTION TO CALL         SHORT DESCRIPTION */
  { .name= "ping",     .session= AFB_SESSION_NONE,  .callback= pingtest,                 .info= "Ping Binding" },
  { .name= "simpleplay", .session= AFB_SESSION_NONE,  .callback= playTest,                 .info= "Ping Binding" },

/*
  { .name= "open",     .session= AFB_SESSION_CREATE,.callback= playerUcmOpen,           .info= "Open a Dedicated SoundCard" },
  { .name= "close",    .session= AFB_SESSION_CLOSE, .callback= playerUcmClose,          .info= "Close previously open SoundCard" },
  
  { .name= "vol-get",       .session= AFB_SESSION_CHECK, .callback= audioLogicGetVol,    .info= "Get Volume [0-100%]" },
  { .name= "vol-set",       .session= AFB_SESSION_CHECK, .callback= audioLogicSetVol,    .info= "Set Volume [0-100%]" },

  { .name= "get-use-case" ,    .session= AFB_SESSION_NONE,  .callback= yerGetUseCase,    .info= "Set Use Case" },
  { .name= "set-use-case" ,    .session= AFB_SESSION_NONE,  .callback= audioPalyerSetUseCase,    .info= "Set Use Case" },
  
  
  { .name= "event-subscribe",  .session= AFB_SESSION_CHECK, .callback= audioPlayerSubscribe, .info= "Subscribe AudioBinding Events" },
*/

  { .name= NULL } /* marker for end of the array */
};

/*
 * description of the binding for afb-daemon
 */
STATIC const struct afb_binding binding_description = {
  /* description conforms to VERSION 1 */
  .type= AFB_BINDING_VERSION_1,
  .v1= {
    .prefix= "player",
    .info= "Sample Player",
    .verbs = binding_verbs
  }
};

// this is call when after all bindings are loaded
PUBLIC int afbBindingV1ServiceInit(struct afb_service service) {
   afbSrv =  service;
   return (0);
};

/*
 * activation function for registering the binding called by afb-daemon
 */
PUBLIC const struct afb_binding *afbBindingV1Register(const struct afb_binding_interface *itf) {
    afbIface= itf;
    
    return &binding_description;	/* returns the description of the binding */
}

