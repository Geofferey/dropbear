/*
 * Dropbear SSH
 * 
 * Copyright (c) 2002,2003 Matt Johnston
 * Copyright (c) 2004 by Mihnea Stoenescu
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include "includes.h"
#include "session.h"
#include "auth.h"
#include "dbutil.h"
#include "buffer.h"
#include "ssh.h"
#include "packet.h"
#include "runopts.h"


void cli_authinitialise() {

	memset(&ses.authstate, 0, sizeof(ses.authstate));
}


/* Send a "none" auth request to get available methods */
void cli_auth_getmethods() {

	TRACE(("enter cli_auth_getmethods"));

	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_USERAUTH_REQUEST);
	buf_putstring(ses.writepayload, cli_opts.username, 
			strlen(cli_opts.username));
	buf_putstring(ses.writepayload, SSH_SERVICE_CONNECTION, 
			SSH_SERVICE_CONNECTION_LEN);
	buf_putstring(ses.writepayload, "none", 4); /* 'none' method */

	encrypt_packet();
	TRACE(("leave cli_auth_getmethods"));

}

void recv_msg_userauth_banner() {

	unsigned char* banner = NULL;
	unsigned int bannerlen;
	unsigned int i, linecount;

	TRACE(("enter recv_msg_userauth_banner"));
	if (ses.authstate.authdone) {
		TRACE(("leave recv_msg_userauth_banner: banner after auth done"));
		return;
	}

	banner = buf_getstring(ses.payload, &bannerlen);
	buf_eatstring(ses.payload); /* The language string */

	if (bannerlen > MAX_BANNER_SIZE) {
		TRACE(("recv_msg_userauth_banner: bannerlen too long: %d", bannerlen));
		goto out;
	}

	cleantext(banner);

	/* Limit to 25 lines */
	linecount = 1;
	for (i = 0; i < bannerlen; i++) {
		if (banner[i] == '\n') {
			if (linecount >= MAX_BANNER_LINES) {
				banner[i] = '\0';
				break;
			}
			linecount++;
		}
	}

	printf("%s\n", banner);

out:
	m_free(banner);
	TRACE(("leave recv_msg_userauth_banner"));
}


void recv_msg_userauth_failure() {

	unsigned char * methods = NULL;
	unsigned char * tok = NULL;
	unsigned int methlen = 0;
	unsigned int partial = 0;
	unsigned int i = 0;

	TRACE(("<- MSG_USERAUTH_FAILURE"));
	TRACE(("enter recv_msg_userauth_failure"));

	if (cli_ses.state != USERAUTH_REQ_SENT) {
		/* Perhaps we should be more fatal? */
		TRACE(("But we didn't send a userauth request!!!!!!"));
		return;
	}

#ifdef ENABLE_CLI_PUBKEY_AUTH
	/* If it was a pubkey auth request, we should cross that key 
	 * off the list. */
	if (cli_ses.lastauthtype == AUTH_TYPE_PUBKEY) {
		cli_pubkeyfail();
	}
#endif

	methods = buf_getstring(ses.payload, &methlen);

	partial = buf_getbyte(ses.payload);

	if (partial) {
		dropbear_log(LOG_INFO, "Authentication partially succeeded, more attempts required");
	} else {
		ses.authstate.failcount++;
	}

	TRACE(("Methods (len %d): '%s'", methlen, methods));

	ses.authstate.authdone=0;
	ses.authstate.authtypes=0;

	/* Split with nulls rather than commas */
	for (i = 0; i < methlen; i++) {
		if (methods[i] == ',') {
			methods[i] = '\0';
		}
	}

	tok = methods; /* tok stores the next method we'll compare */
	for (i = 0; i <= methlen; i++) {
		if (methods[i] == '\0') {
			TRACE(("auth method '%s'", tok));
#ifdef ENABLE_CLI_PUBKEY_AUTH
			if (strncmp(AUTH_METHOD_PUBKEY, tok,
				AUTH_METHOD_PUBKEY_LEN) == 0) {
				ses.authstate.authtypes |= AUTH_TYPE_PUBKEY;
			}
#endif
#ifdef ENABLE_CLI_PASSWORD_AUTH
			if (strncmp(AUTH_METHOD_PASSWORD, tok,
				AUTH_METHOD_PASSWORD_LEN) == 0) {
				ses.authstate.authtypes |= AUTH_TYPE_PASSWORD;
			}
#endif
			tok = &methods[i+1]; /* Must make sure we don't use it after the
									last loop, since it'll point to something
									undefined */
		}
	}

	cli_ses.state = USERAUTH_FAIL_RCVD;
		
	TRACE(("leave recv_msg_userauth_failure"));
}

void recv_msg_userauth_success() {
	TRACE(("received msg_userauth_success"));
	ses.authstate.authdone = 1;
	cli_ses.state = USERAUTH_SUCCESS_RCVD;
}

void cli_auth_try() {

	TRACE(("enter cli_auth_try"));
	int finished = 0;

	CHECKCLEARTOWRITE();
	
	/* XXX We hardcode that we try a pubkey first */
#ifdef ENABLE_CLI_PUBKEY_AUTH
	if (ses.authstate.authtypes & AUTH_TYPE_PUBKEY) {
		finished = cli_auth_pubkey();
		cli_ses.lastauthtype = AUTH_TYPE_PUBKEY;
	}
#endif

#ifdef ENABLE_CLI_PASSWORD_AUTH
	if (!finished && ses.authstate.authtypes & AUTH_TYPE_PASSWORD) {
		finished = cli_auth_password();
		cli_ses.lastauthtype = AUTH_TYPE_PASSWORD;
	}
#endif

	if (!finished) {
		dropbear_exit("No auth methods could be used.");
	}

	TRACE(("leave cli_auth_try"));
}