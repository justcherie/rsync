/*
 * Compatibility routines for older rsync protocol versions.
 *
 * Copyright (C) Andrew Tridgell 1996
 * Copyright (C) Paul Mackerras 1996
 * Copyright (C) 2004-2007 Wayne Davison
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, visit the http://fsf.org website.
 */

#include "rsync.h"

int remote_protocol = 0;
int file_extra_cnt = 0; /* count of file-list extras that everyone gets */
int inc_recurse = 0;

extern int verbose;
extern int am_server;
extern int am_sender;
extern int local_server;
extern int inplace;
extern int recurse;
extern int use_qsort;
extern int allow_inc_recurse;
extern int append_mode;
extern int fuzzy_basis;
extern int read_batch;
extern int delay_updates;
extern int checksum_seed;
extern int basis_dir_cnt;
extern int prune_empty_dirs;
extern int protocol_version;
extern int protect_args;
extern int preserve_uid;
extern int preserve_gid;
extern int preserve_acls;
extern int preserve_xattrs;
extern int need_messages_from_generator;
extern int delete_mode, delete_before, delete_during, delete_after;
extern char *shell_cmd; /* contains VER.SUB string if client is a pre-release */
extern char *partial_dir;
extern char *dest_option;
extern char *files_from;
extern char *filesfrom_host;
extern struct filter_list_struct filter_list;
#ifdef ICONV_OPTION
extern char *iconv_opt;
extern iconv_t ic_send, ic_recv;
#endif

/* These index values are for the file-list's extra-attribute array. */
int uid_ndx, gid_ndx, acls_ndx, xattrs_ndx;
#ifdef ICONV_OPTION
int ic_ndx;

int filesfrom_convert = 0;
#endif

/* The server makes sure that if either side only supports a pre-release
 * version of a protocol, that both sides must speak a compatible version
 * of that protocol for it to be advertised as available. */
static void check_sub_protocol(void)
{
	char *dot;
	int their_protocol, their_sub;
#if SUBPROTOCOL_VERSION != 0
	int our_sub = protocol_version < PROTOCOL_VERSION ? 0 : SUBPROTOCOL_VERSION;
#else
	int our_sub = 0;
#endif

	if (!shell_cmd || !(dot = strchr(shell_cmd, '.'))
	 || !(their_protocol = atoi(shell_cmd))
	 || !(their_sub = atoi(dot+1))) {
#if SUBPROTOCOL_VERSION != 0
		if (our_sub)
			protocol_version--;
#endif
		return;
	}

	if (their_protocol < protocol_version) {
		if (their_sub)
			protocol_version = their_protocol - 1;
		return;
	}

	if (their_protocol > protocol_version)
		their_sub = 0; /* 0 == final version of older protocol */
	if (their_sub != our_sub)
		protocol_version--;
}

void setup_protocol(int f_out,int f_in)
{
	if (am_sender)
		file_extra_cnt += PTR_EXTRA_CNT;
	else
		file_extra_cnt++;
	if (preserve_uid)
		uid_ndx = ++file_extra_cnt;
	if (preserve_gid)
		gid_ndx = ++file_extra_cnt;
	if (preserve_acls && !am_sender)
		acls_ndx = ++file_extra_cnt;
	if (preserve_xattrs)
		xattrs_ndx = ++file_extra_cnt;

	if (remote_protocol == 0) {
		if (am_server && !local_server)
			check_sub_protocol();
		if (!read_batch)
			write_int(f_out, protocol_version);
		remote_protocol = read_int(f_in);
		if (protocol_version > remote_protocol)
			protocol_version = remote_protocol;
	}
	if (read_batch && remote_protocol > protocol_version) {
		rprintf(FERROR, "The protocol version in the batch file is too new (%d > %d).\n",
			remote_protocol, protocol_version);
		exit_cleanup(RERR_PROTOCOL);
	}

	if (verbose > 3) {
		rprintf(FINFO, "(%s) Protocol versions: remote=%d, negotiated=%d\n",
			am_server? "Server" : "Client", remote_protocol, protocol_version);
	}
	if (remote_protocol < MIN_PROTOCOL_VERSION
	 || remote_protocol > MAX_PROTOCOL_VERSION) {
		rprintf(FERROR,"protocol version mismatch -- is your shell clean?\n");
		rprintf(FERROR,"(see the rsync man page for an explanation)\n");
		exit_cleanup(RERR_PROTOCOL);
	}
	if (remote_protocol < OLD_PROTOCOL_VERSION) {
		rprintf(FINFO,"%s is very old version of rsync, upgrade recommended.\n",
			am_server? "Client" : "Server");
	}
	if (protocol_version < MIN_PROTOCOL_VERSION) {
		rprintf(FERROR, "--protocol must be at least %d on the %s.\n",
			MIN_PROTOCOL_VERSION, am_server? "Server" : "Client");
		exit_cleanup(RERR_PROTOCOL);
	}
	if (protocol_version > PROTOCOL_VERSION) {
		rprintf(FERROR, "--protocol must be no more than %d on the %s.\n",
			PROTOCOL_VERSION, am_server? "Server" : "Client");
		exit_cleanup(RERR_PROTOCOL);
	}

	if (protocol_version < 30) {
		if (append_mode == 1)
			append_mode = 2;
		if (preserve_acls && !local_server) {
			rprintf(FERROR,
			    "--acls requires protocol 30 or higher"
			    " (negotiated %d).\n",
			    protocol_version);
			exit_cleanup(RERR_PROTOCOL);
		}
		if (preserve_xattrs && !local_server) {
			rprintf(FERROR,
			    "--xattrs requires protocol 30 or higher"
			    " (negotiated %d).\n",
			    protocol_version);
			exit_cleanup(RERR_PROTOCOL);
		}
	}

	if (delete_mode && !(delete_before+delete_during+delete_after)) {
		if (protocol_version < 30)
			delete_before = 1;
		else
			delete_during = 1;
	}

	if (protocol_version < 29) {
		if (fuzzy_basis) {
			rprintf(FERROR,
			    "--fuzzy requires protocol 29 or higher"
			    " (negotiated %d).\n",
			    protocol_version);
			exit_cleanup(RERR_PROTOCOL);
		}

		if (basis_dir_cnt && inplace) {
			rprintf(FERROR,
			    "%s with --inplace requires protocol 29 or higher"
			    " (negotiated %d).\n",
			    dest_option, protocol_version);
			exit_cleanup(RERR_PROTOCOL);
		}

		if (basis_dir_cnt > 1) {
			rprintf(FERROR,
			    "Using more than one %s option requires protocol"
			    " 29 or higher (negotiated %d).\n",
			    dest_option, protocol_version);
			exit_cleanup(RERR_PROTOCOL);
		}

		if (prune_empty_dirs) {
			rprintf(FERROR,
			    "--prune-empty-dirs requires protocol 29 or higher"
			    " (negotiated %d).\n",
			    protocol_version);
			exit_cleanup(RERR_PROTOCOL);
		}
	} else if (protocol_version >= 30) {
		if (recurse && allow_inc_recurse
		 && !delete_before && !delete_after && !delay_updates
		 && !use_qsort && !prune_empty_dirs)
			inc_recurse = 1;
		if (am_server || read_batch) {
			int i_r = read_byte(f_in);
			if (i_r && !inc_recurse) {
				fprintf(stderr,
				    "Incompatible options specified for inc-recursive %s.\n",
				    read_batch ? "batch file" : "connection");
				exit_cleanup(RERR_SYNTAX);
			}
			inc_recurse = i_r;
		} else
			write_byte(f_out, inc_recurse);
		need_messages_from_generator = 1;
	}

#ifdef ICONV_OPTION
	if (iconv_opt && (!am_sender || inc_recurse))
		ic_ndx = ++file_extra_cnt;
#endif

	if (partial_dir && *partial_dir != '/' && (!am_server || local_server)) {
		int flags = MATCHFLG_NO_PREFIXES | MATCHFLG_DIRECTORY;
		if (!am_sender || protocol_version >= 30)
			flags |= MATCHFLG_PERISHABLE;
		parse_rule(&filter_list, partial_dir, flags, 0);
	}


#ifdef ICONV_OPTION
	if (protect_args && files_from) {
		if (am_sender)
			filesfrom_convert = filesfrom_host && ic_send != (iconv_t)-1;
		else
			filesfrom_convert = !filesfrom_host && ic_recv != (iconv_t)-1;
	}
#endif

	if (am_server) {
		if (!checksum_seed)
			checksum_seed = time(NULL);
		write_int(f_out, checksum_seed);
	} else {
		checksum_seed = read_int(f_in);
	}
}
