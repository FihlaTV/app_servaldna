/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 2012 Daniel O'Connor <daniel@servalproject.org>
*
* See http://www.asterisk.org for more information about
* the Asterisk project. Please do not directly contact
* any of the maintainers of this project for assistance;
* the project provides a web site, mailing lists and IRC
* channels for your use.
*
* This program is free software, distributed under the terms of
* the GNU General Public License Version 2. See the LICENSE file
* at the top of the source tree.
*/


#include <stdio.h>
#include <string.h>
#include <time.h>

#include "asterisk.h"
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/causes.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/causes.h"
#include "asterisk/devicestate.h"
#include <asterisk/dsp.h>
#include <asterisk/ulaw.h>

#include "app.h"
#include "socket.h"
#include "monitor-client.h"
#include "constants.h"

static struct ast_channel  *vomp_request(const char *type, struct ast_format_cap *cap, 
    const struct ast_channel *requestor, const char *addr, int *cause);
static int vomp_call(struct ast_channel *ast, const char *dest, int timeout);
static int vomp_hangup(struct ast_channel *ast);
static int vomp_answer(struct ast_channel *ast);
static struct ast_frame *vomp_read(struct ast_channel *ast);
static int vomp_write(struct ast_channel *ast, struct ast_frame *frame);
static int vomp_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen);
static int vomp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static struct vomp_channel *get_channel(char *token);

static void send_hangup(int session_id);
static void send_ringing(struct vomp_channel *vomp_state);
static void send_pickup(struct vomp_channel *vomp_state);
static void send_call(const char *sid, const char *caller_id, const char *remote_ext);
static void send_audio(struct vomp_channel *vomp_state, unsigned char *buffer, int len, int codec, int time, int sequence);
static void send_lookup_response(const char *sid, const char *port, const char *ext, const char *name);

static int remote_dialing(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_call(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_pickup(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_hangup(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_audio(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_codecs(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_ringing(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_noop(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);
static int remote_lookup(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context);

char *incoming_context = "servald-in";

//AST_MUTEX_DEFINE_STATIC(vomplock); 
int monitor_client_fd=-1;
int monitor_resolve_numbers;

static struct ast_channel_tech vomp_tech = {
	.type             = "VOMP",
	.description      = "Serval Vomp Channel Driver",
	// struct ast_format_cap * capabilities (need to initialise dynamically?)
	.properties       = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER,
	.requester        = vomp_request,
	.call             = vomp_call,
	.hangup           = vomp_hangup,
	.answer           = vomp_answer,
	.read             = vomp_read,
	.write            = vomp_write,
	.indicate         = vomp_indicate,
	.fixup            = vomp_fixup,
};

static struct ast_jb_conf jbconf =
{
	.flags = AST_JB_ENABLED,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};

struct vomp_channel {
	int session_id; // call session id as returned by servald, used as the key for the channels collection
	int chan_id; // unique number for generating a name for the channel
	int channel_start; // time when we started the channel
	int call_start; // time when we hit in-call
	int initiated; // did asterisk start dialing?
	struct ast_channel *owner;
};

struct vomp_channel *dialed_call;

struct monitor_command_handler monitor_handlers[]={
	{.command="CALLFROM",      .handler=remote_call},
	{.command="RINGING",       .handler=remote_ringing},
	{.command="ANSWERED",      .handler=remote_pickup},
	{.command="CALLTO",        .handler=remote_dialing},
	{.command="HANGUP",        .handler=remote_hangup},
	{.command="AUDIO",         .handler=remote_audio},
	{.command="CODECS",        .handler=remote_codecs},
	{.command="LOOKUP",        .handler=remote_lookup},
	{.command="KEEPALIVE",     .handler=remote_noop},
	{.command="CALLSTATUS",    .handler=remote_noop},
	{.command="MONITORSTATUS", .handler=remote_noop},
	{.command="MONITOR",       .handler=remote_noop},
	{.command="INFO",          .handler=remote_noop},
};

int chan_id=0;
// id for the monitor thread
pthread_t thread;

static struct ao2_container *channels;

static long long gettime_ms(void)
{
	struct timeval nowtv;
	gettimeofday(&nowtv, NULL);
	return nowtv.tv_sec * 1000LL + nowtv.tv_usec / 1000;
}

static void vomp_channel_destructor(void *obj){
	// noop...
}

static struct vomp_channel *new_vomp_channel(void){
	struct vomp_channel *vomp_state;
	vomp_state = ao2_alloc(sizeof(struct vomp_channel), vomp_channel_destructor);
	
	// allocate a unique number for this channel
	vomp_state->chan_id = ast_atomic_fetchadd_int(&chan_id, +1);
	vomp_state->channel_start = gettime_ms();
	
	return vomp_state;
}

static void set_session_id(struct vomp_channel *vomp_state, int session_id){
	ast_log(LOG_WARNING, "Adding session %06x\n",session_id);
	vomp_state->session_id = session_id;
	ao2_link(channels, vomp_state);
}

static struct ast_channel *new_channel(struct vomp_channel *vomp_state, const int state, const char *context, const char *ext){
	struct ast_channel *ast = NULL;
	if (vomp_state->owner)
		return vomp_state->owner;
	
	ao2_lock(vomp_state);
	
	struct ast_format_cap *cap = ast_format_cap_alloc(); //TODO AST_FORMAT_CAP_FLAG_DEFAULT
	if (cap){
		ast = ast_channel_alloc(1, state, NULL, NULL, NULL, ext, context, NULL, 0, "VoMP/%08x", vomp_state->chan_id);
		
		struct ast_format tmpfmt;
		ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0);
		
		ast_format_cap_add(cap, &tmpfmt);
		ast_channel_nativeformats_set(ast, cap);
		
		ast_set_read_format(ast, &tmpfmt);
		ast_set_write_format(ast, &tmpfmt);
		
		ast_channel_tech_set(ast, &vomp_tech);
		ast_channel_tech_pvt_set(ast, vomp_state);
		vomp_state->owner = ast; // add ref?
		
		ast_jb_configure(ast, &jbconf);
		
		ast_channel_unlock(ast);
	}
	ao2_unlock(vomp_state);
	return ast;
}


// functions for handling incoming vomp events

// find the channel struct from the servald token
// note that ao2_find adds a reference to the returned object that must be released
struct vomp_channel *get_channel(char *token){
	struct vomp_channel search={
		.session_id=strtol(token, NULL, 16),
	};
	struct vomp_channel *ret = ao2_find(channels, &search, OBJ_POINTER);
	if (ret==NULL)
		ast_log(LOG_WARNING, "Failed to find call structure for session %s (%06x)\n",token,search.session_id);
	return ret;
}

// Send outgoing monitor messages

// TODO fix servald, commands are currently case sensitive
static void send_hangup(int session_id){
	monitor_client_writeline(monitor_client_fd, "hangup %06x\n",session_id);
}
static void send_ringing(struct vomp_channel *vomp_state){
	monitor_client_writeline(monitor_client_fd, "ringing %06x\n",vomp_state->session_id);
}
static void send_pickup(struct vomp_channel *vomp_state){
	monitor_client_writeline(monitor_client_fd, "pickup %06x\n",vomp_state->session_id);
}
static void send_call(const char *sid, const char *caller_id, const char *remote_ext){
	monitor_client_writeline(monitor_client_fd, "call %s %s %s\n", sid, caller_id, remote_ext);
}
static void send_audio(struct vomp_channel *vomp_state, unsigned char *buffer, int len, int codec, int time, int sequence){
	monitor_client_writeline_and_data(monitor_client_fd, buffer, len, "audio %06x %d %d %d\n", 
					  vomp_state->session_id, codec, time, sequence);
}
static void send_lookup_response(const char *sid, const char *port, const char *ext, const char *name){
	ast_log(LOG_WARNING, "lookup match \"%s\" \"%s\" \"%s\" \"%s\"\n", sid, port, ext, name);
	monitor_client_writeline(monitor_client_fd, "lookup match %s %s %s %s\n", sid, port, ext, name);
}

// CALLTO [token] [localsid] [localdid] [remotesid] [remotedid]
// sent so that we can link an outgoing call to a servald session id
static int remote_dialing(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	ast_log(LOG_WARNING, "remote_dialing\n");
	if (!dialed_call)
		return 0;
	// add the vomp state to our collection so we can find it later
	set_session_id(dialed_call, strtol(argv[0], NULL, 16));
	dialed_call=NULL;
	return 1;
}

// CALLFROM [token] [localsid] [localdid] [remotesid] [remotedid]
static int remote_call(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	// TODO fix servald and other VOMP clients to pass extension correctly
	// TODO add callerid...
	char *ext = argv[2];
	int session_id=strtol(argv[0], NULL, 16);
	
	if (ast_exists_extension(NULL, incoming_context, ext, 1, NULL)) {
		struct vomp_channel *vomp_state=new_vomp_channel();
		set_session_id(vomp_state, session_id);
		vomp_state->initiated=0;
		
		struct ast_channel *ast = new_channel(vomp_state, AST_STATE_RINGING, incoming_context, ext);
		ast_log(LOG_WARNING, "Placing call to %s@%s\n", ext, incoming_context);
		if (ast_pbx_start(ast)) {
			ast_channel_hangupcause_set(ast, AST_CAUSE_SWITCH_CONGESTION);
			ast_log(LOG_WARNING, "pbx_start failed, hanging up\n");
			ast_queue_hangup(ast);
			return 0;
		}
		return 1;
	}
	ast_log(LOG_ERROR, "Extension \"%s\" not found\n", ext);
	send_hangup(session_id);
	return 0;
}

static int remote_lookup(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	if (monitor_resolve_numbers){
		char *sid = argv[0];
		char *port = argv[1];
		char *ext = argv[2];
		ast_log(LOG_WARNING, "remote_lookup %s, %s, %s\n", sid, port, ext);
		if (ast_exists_extension(NULL, incoming_context, ext, 1, NULL)) {
			send_lookup_response(sid, port, ext, "");
		}
	}
	return 1;
}

static int remote_pickup(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	int ret=0;
	ast_log(LOG_WARNING, "remote_pickup\n");
	struct vomp_channel *vomp_state=get_channel(argv[0]);
	if (vomp_state){
		if (vomp_state->owner){
			// stop any audio indications on the channel
			ast_indicate(vomp_state->owner, -1);
			// yay, we're INCALL
			ast_queue_control(vomp_state->owner, AST_CONTROL_ANSWER);
			ret=1;
		}
	}
	return ret;
}

static int remote_hangup(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	int ret=0;
	ast_log(LOG_WARNING, "remote_hangup\n");
	struct vomp_channel *vomp_state=get_channel(argv[0]);
	if (vomp_state){
		if (vomp_state->owner){
			// ask asterisk to hangup the channel
			// that way we can let vomp_hangup do all the work to release memory
			ast_queue_hangup(vomp_state->owner);
			ret=1;
		}
	}
	return ret;
}

static int remote_audio(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	int ret=0;
	struct vomp_channel *vomp_state=get_channel(argv[0]);
	if (vomp_state){
		if (vomp_state->owner){
			int codec = strtol(argv[1], NULL, 10);
			int start_time = strtol(argv[2], NULL, 10);
			int sequence = strtol(argv[3], NULL, 10);
			
			struct ast_frame f = {
				.frametype = AST_FRAME_VOICE,
				.flags = AST_FRFLAG_HAS_TIMING_INFO,
				.src = "vomp_call",
				.data.ptr = data,
				.datalen = dataLen,
				.ts = start_time -1 +20,
				.seqno = sequence,
			};
			
			switch (codec){
				case VOMP_CODEC_ULAW:
					ast_format_set(&f.subclass.format, AST_FORMAT_ULAW, 0);
					f.len = dataLen/8;
					f.samples = dataLen;
					break;
				case VOMP_CODEC_ALAW:
					ast_format_set(&f.subclass.format, AST_FORMAT_ALAW, 0);
					f.len = dataLen/8;
					f.samples = dataLen;
					break;
				case VOMP_CODEC_16SIGNED:
					ast_format_set(&f.subclass.format, AST_FORMAT_SLINEAR16, 0);
					f.len = dataLen/16;
					f.samples = dataLen / sizeof(int16_t);
					break;
				case VOMP_CODEC_GSM:
					ast_format_set(&f.subclass.format, AST_FORMAT_GSM, 0);
					break;
				default:
					return 0;
			}
			
			if (ast_format_cmp(&f.subclass.format, ast_channel_readformat(vomp_state->owner)) != AST_FORMAT_CMP_EQUAL){
				// force audio transcoding paths to be rebuilt (I think...)
				ast_set_read_format(vomp_state->owner, &f.subclass.format);
			}
			
			ast_queue_frame(vomp_state->owner, &f);
			ret=1;
		}
	}
	return ret;
}

static int remote_codecs(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	struct vomp_channel *vomp_state=get_channel(argv[0]);
	if (vomp_state){
		int i;
		struct ast_format_cap *cap = ast_format_cap_alloc(); // TODO AST_FORMAT_CAP_FLAG_DEFAULT
		if (cap){
			struct ast_format tmpfmt;
			for (i=1;i<argc;i++){
				int codec = atoi(argv[i]);
				switch(codec){
					case VOMP_CODEC_ULAW:
						ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0);
						break;
					case VOMP_CODEC_ALAW:
						ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0);
						break;
					case VOMP_CODEC_16SIGNED:
						ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0);
						break;
					case VOMP_CODEC_GSM:
						ast_format_set(&tmpfmt, AST_FORMAT_GSM, 0);
						break;
					default:
						continue;
				}
				ast_format_cap_add(cap, &tmpfmt);
			}
			ast_channel_nativeformats_set(vomp_state->owner, cap);
			ast_best_codec(cap, &tmpfmt);
			ast_set_write_format(vomp_state->owner, &tmpfmt);
		}
	}
	return 1;
}

// remote party has started ringing
static int remote_ringing(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	int ret=0;
	ast_log(LOG_WARNING, "remote_ringing\n");
	struct vomp_channel *vomp_state=get_channel(argv[0]);
	if (vomp_state){
		if (vomp_state->owner){
			ast_indicate(vomp_state->owner, AST_CONTROL_RINGING);
			ast_queue_control(vomp_state->owner, AST_CONTROL_RINGING);
			ret=1;
		}
	}
	return ret;
}

static int remote_noop(char *cmd, int argc, char **argv, unsigned char *data, int dataLen, void *context){
	// NOOP for now, just to eliminate unnecessary log spam
	return 1;
}

// thread function for the monitor client
// reads and processes incoming messages
static void *vomp_monitor(void *ignored){
	struct monitor_state *state;
	
	while (1){
		pthread_testcancel();
		ast_log(LOG_WARNING, "opening monitor connection\n");
		monitor_client_fd = monitor_client_open(&state);
		
		if (monitor_client_fd<0){
			ast_log(LOG_ERROR, "Failed to open monitor connection, please start servald\n");
			sleep(10);
			continue;
		}
		
		ast_log(LOG_WARNING, "sending monitor vomp command\n");
		monitor_client_writeline(monitor_client_fd, "monitor vomp %d %d %d %d\n",
					 VOMP_CODEC_16SIGNED, VOMP_CODEC_ULAW, VOMP_CODEC_ALAW, VOMP_CODEC_GSM);
	  
		if (monitor_resolve_numbers)
			monitor_client_writeline(monitor_client_fd, "monitor dnahelper\n");
	  
		ast_log(LOG_WARNING, "reading monitor events\n");
		for(;;){
			pthread_testcancel();
			if (monitor_client_read(monitor_client_fd, state, monitor_handlers, 
						sizeof(monitor_handlers)/sizeof(struct monitor_command_handler))<0){
				break;
			}
		}
		ast_log(LOG_WARNING, "closing monitor connection\n");
		monitor_client_close(monitor_client_fd, state);
		monitor_client_fd=-1;
		sleep(1);
	}
	return NULL;
}



// functions for handling incoming asterisk events

// create a channel for a new outgoing call
static struct ast_channel  *vomp_request(const char *type, struct ast_format_cap *cap, 
  const struct ast_channel *requestor, const char *addr, int *cause){
	// assume addr = servald subscriber id (sid)
	char sid[64], did[64];
	int i=0;
	for (;i<sizeof(sid) && addr[i] && addr[i]!='/';i++)
		sid[i]=addr[i];
	
	sid[i]=0;
	
	// copy the phone number from the last path segment
	addr+=i;
	i=0;
	if (*addr++){
		for (;i<sizeof(did) && addr[i];i++){
			if (addr[i]=='/'){
				// start copying again from the beginning
				addr+=i+1;
				i=-1;
			}else{
				did[i]=addr[i];
			}
		}
	}
	did[i]=0;
	
	ast_log(LOG_WARNING, "vomp_request %s/%s\n", type, sid);
	struct vomp_channel *vomp_state=new_vomp_channel();
	
	// TODO?
	//struct ast_callid *callid = ast_read_threadstorage_callid();
	//ast_format_cap_append_from_cap(p->prefcaps, cap, AST_MEDIA_TYPE_UNKNOWN);
	//ast_format_cap_get_compatible(cap, p->caps, p->jointcaps);
	
	vomp_state->initiated=1;
	struct ast_channel *ast = new_channel(vomp_state, AST_STATE_DOWN, NULL, NULL);
	
	dialed_call = vomp_state;
	
	send_call(sid,"1",did);
	
	return ast;
}

static int vomp_hangup(struct ast_channel *ast){
	ast_log(LOG_WARNING, "vomp_hangup %s\n", ast_channel_name(ast));
	
	struct vomp_channel *vomp_state = ast_channel_tech_pvt(ast);
	ao2_lock(vomp_state);
	
	send_hangup(vomp_state->session_id);
	ao2_unlink(channels, vomp_state);
	ast_channel_tech_set(ast, NULL);
	ast_channel_tech_pvt_set(ast, NULL);
	vomp_state->owner = NULL;
	ao2_unlock(vomp_state);
	return 0;
}

static int vomp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan){
	struct vomp_channel *vomp_state = ast_channel_tech_pvt(newchan);
	ast_log(LOG_WARNING, "vomp_fixup %s %s\n", ast_channel_name(oldchan), ast_channel_name(newchan));
	vomp_state->owner = newchan;
	return 0;
}

static int vomp_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen){
	ast_log(LOG_WARNING, "vomp_indicate %d condition on channel %s\n", 
			ind, ast_channel_name(ast));
	// return -1 and asterisk will generate audible tones.
	
	struct vomp_channel *vomp_state = ast_channel_tech_pvt(ast);
	switch(ind){
		case AST_CONTROL_PROGRESS:
		case AST_CONTROL_RINGING:
			if (!vomp_state->initiated)
				send_ringing(vomp_state);
			break;
			
		case AST_CONTROL_BUSY:
		case AST_CONTROL_CONGESTION:
			send_hangup(vomp_state->session_id);
			break;
			
		default:
			return -1;
	}
	return 0;
}

static int vomp_call(struct ast_channel *ast, const char *dest, int timeout){
	// NOOP, as we have already started the call in vomp_request
	ast_log(LOG_WARNING, "vomp_call %s %s\n", ast_channel_name(ast), dest);
	
	return 0;
}

static int vomp_answer(struct ast_channel *ast){
	ast_log(LOG_WARNING, "vomp_answer %s\n", ast_channel_name(ast));
	
	struct vomp_channel *vomp_state = ast_channel_tech_pvt(ast);
	
	vomp_state->call_start = gettime_ms();
	ast_setstate(ast, AST_STATE_UP);
	send_pickup(vomp_state);
	return 0;
}

static struct ast_frame *vomp_read(struct ast_channel *ast){
	// this method should only be called if we told asterisk to monitor a file for us.
	
	ast_log(LOG_WARNING, "vomp_read %s - this shouldn't happen\n", ast_channel_name(ast));
	
	return &ast_null_frame;
}

static int vomp_write(struct ast_channel *ast, struct ast_frame *frame){
	struct vomp_channel *vomp_state = ast_channel_tech_pvt(ast);
	if (!vomp_state)
		return 0;
	switch (frame->frametype){
		case AST_FRAME_VOICE:{
			int audio_codec, audio_time=-1, audio_sequence=-1;
			switch (frame->subclass.format.id){
				case AST_FORMAT_ULAW:
					audio_codec = VOMP_CODEC_ULAW;
					break;
				case AST_FORMAT_ALAW:
					audio_codec = VOMP_CODEC_ALAW;
					break;
				case AST_FORMAT_SLINEAR:
					audio_codec = VOMP_CODEC_16SIGNED;
					break;
				case AST_FORMAT_GSM:
					audio_codec = VOMP_CODEC_GSM;
					break;
				default:
					return 0;
			}
			
			if (frame->flags & AST_FRFLAG_HAS_TIMING_INFO){
				audio_time=frame->ts;
				audio_sequence=frame->seqno;
			}
			
			send_audio(vomp_state, frame->data.ptr, frame->datalen, audio_codec, audio_time, audio_sequence);
		break;}
		default:
			break;
	}
	return 0;
}

static int vomp_hash(const void *obj, const int flags){
	const struct vomp_channel *vomp_state = obj;
	return vomp_state->session_id;
}

static int vomp_compare(void *obj, void *arg, int flags){
	struct vomp_channel *obj1 = obj;
	struct vomp_channel *obj2 = arg;
	return obj1->session_id==obj2->session_id ? CMP_MATCH | CMP_STOP : 0;
}

// module load / unload
int vomp_register_channel(void){
	ast_log(LOG_WARNING, "Registering Serval channel driver\n");
	vomp_tech.capabilities = ast_format_cap_alloc();// TODO AST_FORMAT_CAP_FLAG_DEFAULT
	if (!vomp_tech.capabilities)
		return AST_MODULE_LOAD_FAILURE;
	
	struct ast_format tmpfmt;
	
	ast_format_cap_add(vomp_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(vomp_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
	ast_format_cap_add(vomp_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0));
	ast_format_cap_add(vomp_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_GSM, 0));
	
	if (ast_channel_register(&vomp_tech)) {
		ao2_cleanup(vomp_tech.capabilities);
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", vomp_tech.type);
		return AST_MODULE_LOAD_FAILURE;
	}
	
	channels=ao2_container_alloc(1, vomp_hash, vomp_compare);
	
	if (ast_pthread_create_background(&thread, NULL, vomp_monitor, NULL)) {
	}
	
	ast_log(LOG_WARNING, "Done\n");
	return 0;
}

int vomp_unregister_channel(void){
	ast_log(LOG_WARNING, "Unregistering Serval channel driver\n");
	
	pthread_cancel(thread);
#ifdef SIGURG
	pthread_kill(thread, SIGURG);
#endif
	pthread_join(thread, NULL);
	
	ast_channel_unregister(&vomp_tech);
	ao2_cleanup(vomp_tech.capabilities);
	vomp_tech.capabilities = NULL;
	ao2_ref(channels, -1);
	channels = NULL;
	ast_log(LOG_WARNING, "Done\n");
	return 0;
}


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

