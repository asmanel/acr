// m2s2c
client *findauth(uint id)
{
	loopv(clients) if(clients[i]->authreq == id) return clients[i];
	return NULL;
}

extern vector<authrequest> authrequests;

inline bool canreqauth(client &ci, int authtoken, int authuser)
{
	const int cn = ci.clientnum;
	if(!isdedicated || !canreachauthserv){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return false;} // not dedicated/connected
	// if(ci.authreq){ sendf(cn, 1, "ri2", N_AUTHCHAL, 1);	return false;	} // already pending
	// if(ci.authmillis + 2000 > servmillis){ sendf(cn, 1, "ri3", N_AUTHCHAL, 6, ci.authmillis + 2000 - servmillis); return false; } // flood check
	return true;
}

int allowconnect(client &ci, const char *pwd = NULL, int authreq = 0, int authuser = 0)
{
	if(ci.type == ST_LOCAL) return DISC_NONE;
	//if(!m_valid(smode)) return DISC_PRIVATE;
	if(ci.priv >= PRIV_ADMIN) return DISC_NONE;
	if(authreq && authuser && canreqauth(ci, authreq, authuser))
	{
		ci.authtoken = authreq;
		ci.authuser = authuser;
		ci.authreq = nextauthreq++;
		if(!ci.authreq)
			ci.authreq = 1;
		ci.connectauth = true;
		logline(ACLOG_INFO, "[%s] %s logged in, requesting auth #%d as %d", gethostname(ci.clientnum), formatname(ci), ci.authreq, authuser);
		return DISC_NONE;
	}
	// nickname list
	int bl = 0, wl = nbl.checknickwhitelist(ci);
	const char *wlp = wl == NWL_PASS ? ", nickname whitelist match" : "";
	if(wl == NWL_UNLISTED) bl = nbl.checknickblacklist(ci.name);
	if(wl == NWL_IPFAIL || wl == NWL_PWDFAIL)
	{ // nickname matches whitelist, but IP is not in the required range or PWD doesn't match
		logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong %s", gethostname(ci.clientnum), formatname(ci), wl == NWL_IPFAIL ? "IP" : "PWD");
		return wl == NWL_IPFAIL ? DISC_NAME_IP : DISC_NAME_PWD;
	}
	else if(bl > 0){ // nickname matches blacklist
		logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", gethostname(ci.clientnum), formatname(ci), bl);
		return DISC_NAME;
	}
	const bool banned = isbanned(ci.clientnum);
	const bool srvfull = numnonlocalclients() > scl.maxclients;
	const bool srvprivate = mastermode >= MM_PRIVATE;
	pwddetail pd;
	if(pwd && checkadmin(ci.name, pwd, ci.salt, &pd) && (pd.priv >= PRIV_ADMIN || (banned && !srvfull && !srvprivate))){ // admin (or master or deban) password match
		bool banremoved = false;
		if(pd.priv) setpriv(ci.clientnum, pd.priv);
		if(banned) loopv(bans) if(bans[i].host == ci.peer->address.host) { banremoved = true; bans.remove(i); break; } // remove admin bans
		logline(ACLOG_INFO, "[%s] %s logged in using the password in line %d%s%s", gethostname(ci.clientnum), formatname(ci), pd.line, wlp, banremoved ? ", (ban removed)" : "");
		return DISC_NONE;
	}
	if(srvprivate) return DISC_PRIVATE;
	if(srvfull) return DISC_FULL;
	if(banned) return DISC_REFUSE;
	// does the master server want a disconnection?
	if(!scl.bypassglobalbans && ci.authpriv < PRIV_NONE && ci.masterverdict) return ci.masterverdict;
	if(pwd && *scl.serverpassword){ // server password required
		if(!strcmp(genpwdhash(ci.name, scl.serverpassword, ci.salt), pwd)){
			logline(ACLOG_INFO, "[%s] %s logged in using the server password%s", gethostname(ci.clientnum), formatname(ci), wlp);
		}
		else return DISC_PASSWORD;
	}
	logline(ACLOG_INFO, "[%s] %s logged in%s", gethostname(ci.clientnum), formatname(ci), wlp);
	return DISC_NONE;
}

void checkauthdisc(client &ci, bool force = false)
{
	if(ci.connectauth || force)
	{
		ci.connectauth = false;
		const int disc = allowconnect(ci);
		if(disc) disconnect_client(ci.clientnum, disc);
	}
}

void authsuceeded(uint id, char priv, char *name){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	filtertext(c->authname, name);
	logline(ACLOG_INFO, "[%s] auth #%d suceeded for %s as '%s'", gethostname(c->clientnum), id, privname(priv), c->authname);
	//bool banremoved = false;
	loopv(bans) if(bans[i].host == c->peer->address.host){ bans.remove(i--); /*banremoved = true;*/ } // deban
	// broadcast "identified" if privileged or a ban was removed
	sendf(-1, 1, "ri3s", N_AUTHCHAL, 5, c->clientnum, c->authname);
	if(priv)
	{
		c->authpriv = clamp<char>(priv, PRIV_MASTER, PRIV_MAX);
		// setpriv(c->clientnum, c->authpriv);
		// unmute if auth has privilege
		c->muted = false;
	}
	else c->authpriv = PRIV_NONE; // bypass master bans
	checkauthdisc(*c); // can bypass passwords
}

void authfail(uint id, bool fail){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "[%s] auth #%d %s!", gethostname(c->clientnum), id, fail ? "failed" : "error occurred");
	sendf(c->clientnum, 1, "ri2", N_AUTHCHAL, 3);
	checkauthdisc(*c);
}

void authchallenged(uint id, int nonce){
	client *c = findauth(id);
	if(!c) return;
	sendf(c->clientnum, 1, "ri3", N_AUTHREQ, nonce, c->authtoken);
	logline(ACLOG_INFO, "[%s] auth #%d challenged by master", gethostname(c->clientnum), id);
}

bool answerchallenge(int cn, int *hash){
	if(!valid_client(cn)) return false;
	client &cl = *clients[cn];
	if(!isdedicated){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return false;}
	if(!cl.authreq) return false;
	loopv(authrequests){
		if(authrequests[i].id == cl.authreq){
			sendf(cn, 1, "ri2", N_AUTHCHAL, 1);
			return false;
		}
	}
	authrequest &r = authrequests.add();
	r.id = cl.authreq;
	memcpy(r.hash, hash, sizeof(int) * 5);
	logline(ACLOG_INFO, "[%s] answers auth #%d", gethostname(cn), r.id);
	sendf(cn, 1, "ri2", N_AUTHCHAL, 4);
	return true;
}

void mastermute(int cn){
	if(!valid_client(cn)) return;
	client &ci = *clients[cn];
	ci.muted = true;
}

void masterverdict(int cn, int result){
	if(!valid_client(cn)) return;
	client &ci = *clients[cn];
	ci.masterverdict = result;
	if(!ci.connectauth && result) checkauthdisc(ci, true);
}

void logversion(client &ci, int ver, int defs, int guid){
	// Filter the definitions
	defs &= 0x80 | 0x40 | 0x20 | 0x08 | 0x04;
	// Make the string
	string cdefs;
	*cdefs = 0;
	if(defs & 0x40) concatstring(cdefs, "W");
	if(defs & 0x20) concatstring(cdefs, "M");
	if(defs & 0x04) concatstring(cdefs, "L");
	if(defs & 0x08) concatstring(cdefs, "D");
	logline(ACLOG_INFO, "[%s] %s runs %d [%X] [GUID-%08X]", gethostname(ci.clientnum), formatname(ci), ci.acversion = ver, ci.acbuildtype = defs, ci.guid = guid);
}