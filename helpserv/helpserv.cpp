// Anope IRC Services <https://www.anope.org/>
//
// SPDX-License-Identifier: GPL-2.0-only
//
// HelpServ: operator desk. AideMoi and SignalMoi collect user tickets;
// HelpServ notifies #_BO / #_logs and is the only bot helpers use.

#include "module.h"

#define HELPSERV_TICKET_TYPE "HelpServTicket"

namespace
{
	const char *const QUEUE_HELP = "HELP";
	const char *const QUEUE_REPORT = "REPORT";
	const char *const STATUS_OPEN = "OPEN";
	const char *const STATUS_ASSIGNED = "ASSIGNED";
	const char *const STATUS_CLOSED = "CLOSED";

	Anope::string SpamKey(const User *u)
	{
		if (!u)
			return "";
		if (u->ip.valid())
		{
			Anope::string ip = u->ip.addr();
			if (!ip.empty() && ip != "0.0.0.0" && ip != "::" && ip != "0")
				return "ip:" + ip;
		}
		if (!u->host.empty())
			return "host:" + u->host;
		return "";
	}

	Anope::string ParseQueueName(const Anope::string &s)
	{
		if (s.equals_ci("HELP") || s.equals_ci("AIDE") || s.equals_ci("AIDEMOI"))
			return QUEUE_HELP;
		if (s.equals_ci("REPORT") || s.equals_ci("SIGNAL") || s.equals_ci("SIGNALEMENT") || s.equals_ci("SIGNALMOI"))
			return QUEUE_REPORT;
		return "";
	}

	Anope::string TicketNoticePrefix(const Anope::string &queue, unsigned id)
	{
		if (queue.equals_ci(QUEUE_REPORT))
			return Anope::Format(Language::Translate(_("[ SIGNALEMENT ] ticket \002#%u\002")), id);
		return Anope::Format(Language::Translate(_("[ DEMANDE D'AIDE ] ticket \002#%u\002")), id);
	}

	Anope::string ChannelNameFromSpec(const Anope::string &spec)
	{
		size_t h = spec.find('#');
		return h == Anope::string::npos ? spec : spec.substr(h);
	}

	bool IsGreeting(const Anope::string &text)
	{
		Anope::string t = text;
		t.trim();
		t = t.lower();
		while (!t.empty() && (t[t.length() - 1] == '!' || t[t.length() - 1] == '.' || t[t.length() - 1] == '?'))
			t.erase(t.length() - 1);
		t.trim();
		return t.equals_ci("hi") || t.equals_ci("hello") || t.equals_ci("hey") || t.equals_ci("yo")
			|| t.equals_ci("bonjour") || t.equals_ci("salut") || t.equals_ci("coucou") || t.equals_ci("bjr")
			|| t.equals_ci("bonsoir") || t.equals_ci("re") || t.equals_ci("slt") || t.equals_ci("cc");
	}

	Anope::string FoldTriageText(const Anope::string &in)
	{
		Anope::string t = in.lower();
		static const char *const pairs[][2] = {
			{"é", "e"}, {"è", "e"}, {"ê", "e"}, {"ë", "e"},
			{"à", "a"}, {"â", "a"}, {"ä", "a"}, {"á", "a"},
			{"ù", "u"}, {"û", "u"}, {"ü", "u"}, {"ú", "u"},
			{"ô", "o"}, {"ö", "o"}, {"ó", "o"},
			{"î", "i"}, {"ï", "i"}, {"í", "i"},
			{"ç", "c"}, {"œ", "oe"}, {"æ", "ae"},
			{"’", "'"}, {"‘", "'"}, {"`", "'"}, {"´", "'"},
		};
		for (const auto &pair : pairs)
			t = t.replace_all_cs(pair[0], pair[1]);

		Anope::string out;
		for (size_t i = 0; i < t.length(); ++i)
		{
			unsigned char c = static_cast<unsigned char>(t[i]);
			if (c == '\'')
				continue;
			if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
				out.push_back(static_cast<char>(c));
			else if (!out.empty() && out[out.length() - 1] != ' ')
				out.push_back(' ');
		}
		out.trim();
		return out;
	}

	bool LooksLikeRequest(const Anope::string &text, unsigned min_len)
	{
		Anope::string t = FoldTriageText(text);
		if (t.empty())
			return false;

		/* Longest phrases first so "j'ai besoin d'aide" is removed as a whole. */
		static const char *const filler[] = {
			"est ce que vous pouvez maider",
			"est ce que tu peux maider",
			"est ce que quelquun peut maider",
			"quelquun pourrait maider",
			"quelquun peut il maider",
			"vous pouvez maider",
			"pouvez vous maider",
			"peux tu maider",
			"tu peux maider",
			"qqn peut maider",
			"quelquun peut maider",
			"jai besoin daide",
			"jaurai besoin daide",
			"jaimerais de laide",
			"je voudrais de laide",
			"je veux de laide",
			"besoin daide",
			"aidez moi sil vous plait",
			"aidez moi svp",
			"aidez moi",
			"aide moi svp",
			"aide moi",
			"help me please",
			"can you help me",
			"can you help",
			"i need help",
			"need help",
			"please help",
			"help me",
			"jai un probleme",
			"jai un soucis",
			"jai un souci",
			"jai une question",
			"jai un pb",
			"comment allez vous",
			"comment vas tu",
			"comment ca va",
			"cest urgent",
			"c urgent",
			"sil vous plait",
			"sil te plait",
			"hello there",
			"how are you",
			"whats up",
			"a laide",
			"au secours",
			"bonjour",
			"bonsoir",
			"salutations",
			"salut",
			"coucou",
			"hello",
			"please",
			"merci",
			"thanks",
			"thx",
			"svp",
			"urgent",
			"urgence",
			"sos",
			"aled",
			"help",
			"hey",
			"yo",
			"hi",
			"bjr",
			"slt",
			"cc",
			"re",
			"wesh",
			"hola",
			"ok",
			"okay",
			"daccord",
			"dac",
			"ca va",
		};

		Anope::string hay = " " + t + " ";
		for (const char *phrase : filler)
			hay = hay.replace_all_cs(Anope::string(" ") + phrase + " ", " ");

		Anope::string remaining;
		bool space = false;
		for (size_t i = 0; i < hay.length(); ++i)
		{
			char c = hay[i];
			if (c == ' ')
			{
				if (!space && !remaining.empty())
					remaining.push_back(' ');
				space = true;
			}
			else
			{
				remaining.push_back(c);
				space = false;
			}
		}
		remaining.trim();
		return remaining.length() >= min_len;
	}

	bool ContainsPadded(const Anope::string &hay, const Anope::string &needle)
	{
		if (hay.empty() || needle.empty())
			return false;
		return (" " + hay + " ").find(" " + needle + " ") != Anope::string::npos;
	}

	bool ContainsAnyPadded(const Anope::string &hay, const std::vector<Anope::string> &needles)
	{
		for (const auto &needle : needles)
		{
			if (ContainsPadded(hay, needle))
				return true;
		}
		return false;
	}

	bool LooksLikeIncident(const Anope::string &folded)
	{
		static const char *const phrases[] = {
			"narrive pas", "narrive plus", "yarrive pas", "yarrive plus", "arrive pas", "arrive plus",
			"peux pas", "peut pas", "plus possible", "impossible",
			"marche pas", "marche plus", "fonctionne pas", "fonctionne plus",
			"erreur", "bug", "crash", "plante", "probleme", "souci",
			"vole", "volee", "derobe", "derobee",
			"banni", "kline", "gline", "akill", "shun",
			"mot de passe", "password",
			"usurpe", "usurpee",
			"nick pris", "pseudo pris", "pseudo vole",
		};
		for (const char *phrase : phrases)
		{
			if (ContainsPadded(folded, phrase))
				return true;
		}
		return false;
	}

	bool LooksLikeHowTo(const Anope::string &folded)
	{
		static const char *const phrases[] = {
			"comment", "comment faire", "comment utiliser", "comment march", "comment fonctionne",
			"cest quoi", "quest ce", "ca sert a quoi", "a quoi sert",
			"tuto", "tutoriel", "documentation", "guide", "utilisation",
			"lien", "url", "page daide", "site daide", "ou trouver", "aide pour",
		};
		for (const char *phrase : phrases)
		{
			if (ContainsPadded(folded, phrase))
				return true;
		}
		return false;
	}

	Anope::string StripNickPunct(const Anope::string &in)
	{
		Anope::string t = in;
		while (!t.empty())
		{
			char c = t[t.length() - 1];
			if (c == ',' || c == '.' || c == '!' || c == '?' || c == ':' || c == ';' || c == '"' || c == '\'')
				t.erase(t.length() - 1);
			else
				break;
		}
		while (!t.empty() && (t[0] == '"' || t[0] == '\'' || t[0] == '<' || t[0] == '('))
			t.erase(t.begin());
		t.trim();
		return t;
	}

	bool IsNickStopWord(const Anope::string &s)
	{
		return s.equals_ci("je") || s.equals_ci("tu") || s.equals_ci("il") || s.equals_ci("elle")
			|| s.equals_ci("on") || s.equals_ci("nous") || s.equals_ci("vous") || s.equals_ci("ils")
			|| s.equals_ci("me") || s.equals_ci("te") || s.equals_ci("le") || s.equals_ci("la")
			|| s.equals_ci("les") || s.equals_ci("un") || s.equals_ci("une") || s.equals_ci("des")
			|| s.equals_ci("ce") || s.equals_ci("cet") || s.equals_ci("cette") || s.equals_ci("ma")
			|| s.equals_ci("mon") || s.equals_ci("mes") || s.equals_ci("son") || s.equals_ci("ses")
			|| s.equals_ci("que") || s.equals_ci("qui") || s.equals_ci("pas") || s.equals_ci("sur")
			|| s.equals_ci("dans") || s.equals_ci("pour") || s.equals_ci("avec") || s.equals_ci("mais")
			|| s.equals_ci("donc") || s.equals_ci("car") || s.equals_ci("the") || s.equals_ci("and")
			|| s.equals_ci("for") || s.equals_ci("not") || s.equals_ci("this") || s.equals_ci("that")
			|| s.equals_ci("bonjour") || s.equals_ci("salut") || s.equals_ci("coucou") || s.equals_ci("hello")
			|| s.equals_ci("aide") || s.equals_ci("help") || s.equals_ci("report") || s.equals_ci("signale")
			|| s.equals_ci("signaler") || s.equals_ci("signalement") || s.equals_ci("ticket") || s.equals_ci("svp")
			|| s.equals_ci("please") || s.equals_ci("merci") || s.equals_ci("pseudo") || s.equals_ci("nick")
			|| s.equals_ci("contre") || s.equals_ci("utilisateur") || s.equals_ci("personne") || s.equals_ci("type");
	}

	bool LooksLikeNickToken(const Anope::string &raw)
	{
		Anope::string s = StripNickPunct(raw);
		if (s.length() < 2 || s.length() > 32)
			return false;
		if (s[0] == '#' || s[0] == ':' || s[0] == '@')
			return false;
		if (IsGreeting(s) || IsNickStopWord(s))
			return false;
		unsigned char first = static_cast<unsigned char>(s[0]);
		if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')
			|| first == '[' || first == ']' || first == '\\' || first == '`'
			|| first == '_' || first == '^' || first == '{' || first == '|' || first == '}'))
			return false;
		for (size_t i = 0; i < s.length(); ++i)
		{
			unsigned char c = static_cast<unsigned char>(s[i]);
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
				|| c == '-' || c == '[' || c == ']' || c == '\\' || c == '`'
				|| c == '_' || c == '^' || c == '{' || c == '|' || c == '}')
				continue;
			return false;
		}
		return true;
	}

	bool LooksLikeUnknownPerson(const Anope::string &text)
	{
		Anope::string t = FoldTriageText(text);
		return t.equals_ci("inconnu") || t.equals_ci("inconnue") || t.equals_ci("quelquun")
			|| t.equals_ci("je sais pas") || t.equals_ci("je ne sais pas") || t.equals_ci("aucune idee")
			|| t.equals_ci("pas de pseudo") || t.equals_ci("nsp") || t.equals_ci("aucun")
			|| t.equals_ci("personne") || t.equals_ci("un inconnu") || t.equals_ci("je connais pas");
	}

	void ExtractReportClues(const Anope::string &message, const User *from, Anope::string &nick, Anope::string &chan)
	{
		nick.clear();
		chan.clear();
		spacesepstream ss(message);
		std::vector<Anope::string> tokens;
		for (Anope::string tok; ss.GetToken(tok);)
		{
			tok = StripNickPunct(tok);
			if (!tok.empty())
				tokens.push_back(tok);
		}
		for (const auto &t : tokens)
		{
			if (t[0] == '#' && t.length() > 1 && chan.empty())
				chan = t;
		}
		for (const auto &t : tokens)
		{
			if (!LooksLikeNickToken(t) || (from && t.equals_ci(from->nick)))
				continue;
			if (User *found = User::Find(t, true))
			{
				if (dynamic_cast<BotInfo *>(found))
					continue;
				nick = t;
				return;
			}
			if (NickAlias::Find(t))
			{
				nick = t;
				return;
			}
		}
		for (const auto &t : tokens)
		{
			if (LooksLikeNickToken(t) && !(from && t.equals_ci(from->nick)))
			{
				nick = t;
				return;
			}
		}
	}

	bool LooksLikeNickOnly(const Anope::string &message)
	{
		spacesepstream ss(message);
		Anope::string tok;
		bool has_nick = false;
		int others = 0;
		while (ss.GetToken(tok))
		{
			tok = StripNickPunct(tok);
			if (tok.empty())
				continue;
			if (tok[0] == '#')
				continue;
			if (LooksLikeNickToken(tok) && !has_nick)
				has_nick = true;
			else
				++others;
		}
		return has_nick && others == 0;
	}

	Anope::string FirstToken(const Anope::string &message)
	{
		Anope::string tok;
		spacesepstream(message).GetToken(tok);
		if (!tok.empty() && tok[0] == '@')
			tok.erase(tok.begin());
		return tok;
	}

	bool HasChanStatus(User *u, Channel *c, const Anope::string &mode)
	{
		if (!u || !c)
			return false;
		auto *memb = c->FindUser(u);
		if (!memb)
			return false;
		auto *cm = ModeManager::FindChannelModeByName(mode);
		return cm && memb->status.HasMode(cm);
	}

	bool HasPrivilegedChanStatus(User *u, Channel *c)
	{
		return HasChanStatus(u, c, "OWNER") || HasChanStatus(u, c, "PROTECT")
			|| HasChanStatus(u, c, "OP") || HasChanStatus(u, c, "HALFOP");
	}

	bool HasChanVoiceAccess(User *u, Channel *c)
	{
		if (!u || !c || !c->ci)
			return false;
		auto ag = c->ci->AccessFor(u);
		return ag.HasPriv("VOICE") || ag.HasPriv("VOICEME");
	}

	bool HasChannelOp(User *u, ChannelInfo *ci)
	{
		if (!u || !ci)
			return false;
		auto ag = ci->AccessFor(u);
		return ag.HasPriv("OP") || ag.HasPriv("HALFOP") || ag.HasPriv("PROTECT")
			|| ag.HasPriv("OWNER") || ag.HasPriv("FOUNDER") || ag.founder;
	}
}

struct TicketLine final
{
	char type = 'U'; // U user, N note, S system
	time_t ts = 0;
	Anope::string nick;
	Anope::string text;
};

class AideTicket;
static std::vector<AideTicket *> Tickets;
static unsigned NextTicketId = 1;

class AideTicket final
	: public Serializable
{
public:
	unsigned id = 0;
	Anope::string queue;
	Anope::string status;
	Anope::string opener_nick;
	Anope::string opener_account;
	Anope::string opener_host;
	Anope::string opener_uid;
	Anope::string opener_ip;
	Anope::string target_nick;
	Anope::string target_account;
	Anope::string category;
	Anope::string channel;
	Anope::string summary;
	Anope::string assignee;
	Anope::string close_reason;
	time_t opened = 0;
	time_t assigned = 0;
	time_t updated = 0;
	time_t closed = 0;
	std::vector<TicketLine> lines;

	AideTicket()
		: Serializable(HELPSERV_TICKET_TYPE)
	{
		Tickets.push_back(this);
	}

	~AideTicket() override
	{
		auto it = std::find(Tickets.begin(), Tickets.end(), this);
		if (it != Tickets.end())
			Tickets.erase(it);
	}

	void AddLine(char type, const Anope::string &nick, const Anope::string &text)
	{
		TicketLine line;
		line.type = type;
		line.ts = Anope::CurTime;
		line.nick = nick;
		line.text = text;
		lines.push_back(line);
		updated = Anope::CurTime;
		QueueUpdate();
	}

	Anope::string EncodeLines() const
	{
		Anope::string out;
		for (const auto &line : lines)
		{
			if (!out.empty())
				out.push_back('\n');
			Anope::string text = line.text;
			text.replace_all_cs("\n", " ");
			out += Anope::ToString(line.type) + " " + Anope::ToString(line.ts) + " " + line.nick + " :" + text;
		}
		return out;
	}

	void DecodeLines(const Anope::string &blob)
	{
		lines.clear();
		sepstream sep(blob, '\n');
		for (Anope::string row; sep.GetToken(row);)
		{
			if (row.length() < 3)
				continue;
			TicketLine line;
			line.type = row[0];
			spacesepstream ss(row.substr(2));
			Anope::string ts, nick;
			if (!ss.GetToken(ts) || !ss.GetToken(nick))
				continue;
			line.ts = Anope::Convert<time_t>(ts, 0);
			line.nick = nick;
			line.text = ss.GetRemaining();
			if (!line.text.empty() && line.text[0] == ':')
				line.text.erase(line.text.begin());
			lines.push_back(line);
		}
	}

	static AideTicket *FindId(unsigned id)
	{
		for (auto *t : Tickets)
		{
			if (t->id == id)
				return t;
		}
		return nullptr;
	}

	static AideTicket *FindOpenFor(const User *u, const Anope::string &queue)
	{
		if (!u)
			return nullptr;
		Anope::string account = u->Account() ? u->Account()->display : "";
		Anope::string key = SpamKey(u);
		for (auto *t : Tickets)
		{
			if (t->queue.equals_ci(queue) && !t->status.equals_ci(STATUS_CLOSED)
				&& (t->opener_uid.equals_ci(u->GetUID())
					|| t->opener_nick.equals_ci(u->nick)
					|| (!account.empty() && t->opener_account.equals_ci(account))))
				return t;
		}
		/* Unregistered reconnects: keep one open ticket per host/IP. */
		if (account.empty() && !key.empty())
		{
			for (auto *t : Tickets)
			{
				if (t->queue.equals_ci(queue) && !t->status.equals_ci(STATUS_CLOSED)
					&& t->opener_account.empty() && t->opener_ip.equals_ci(key))
					return t;
			}
		}
		return nullptr;
	}
};

class AideTicketType final
	: public Serialize::Type
{
public:
	AideTicketType(Module *owner)
		: Serialize::Type(HELPSERV_TICKET_TYPE, owner)
	{
	}

	void Serialize(Serializable *obj, Serialize::Data &data) const override
	{
		const auto *t = static_cast<const AideTicket *>(obj);
		data.Store("id", t->id);
		data.Store("queue", t->queue);
		data.Store("status", t->status);
		data.Store("opener_nick", t->opener_nick);
		data.Store("opener_account", t->opener_account);
		data.Store("opener_host", t->opener_host);
		data.Store("opener_uid", t->opener_uid);
		data.Store("opener_ip", t->opener_ip);
		data.Store("target_nick", t->target_nick);
		data.Store("target_account", t->target_account);
		data.Store("category", t->category);
		data.Store("channel", t->channel);
		data.Store("summary", t->summary);
		data.Store("assignee", t->assignee);
		data.Store("close_reason", t->close_reason);
		data.Store("opened", t->opened);
		data.Store("assigned", t->assigned);
		data.Store("updated", t->updated);
		data.Store("closed", t->closed);
		data.Store("lines", t->EncodeLines());
	}

	Serializable *Unserialize(Serializable *obj, Serialize::Data &data) const override
	{
		AideTicket *t;
		if (obj)
			t = anope_dynamic_static_cast<AideTicket *>(obj);
		else
			t = new AideTicket();

		t->id = data.Load<unsigned>("id");
		t->queue = data.Load("queue");
		t->status = data.Load("status");
		t->opener_nick = data.Load("opener_nick");
		t->opener_account = data.Load("opener_account");
		t->opener_host = data.Load("opener_host");
		t->opener_uid = data.Load("opener_uid");
		t->opener_ip = data.Load("opener_ip");
		t->target_nick = data.Load("target_nick");
		t->target_account = data.Load("target_account");
		t->category = data.Load("category");
		t->channel = data.Load("channel");
		t->summary = data.Load("summary");
		t->assignee = data.Load("assignee");
		t->close_reason = data.Load("close_reason");
		t->opened = data.Load<time_t>("opened");
		t->assigned = data.Load<time_t>("assigned");
		t->updated = data.Load<time_t>("updated");
		t->closed = data.Load<time_t>("closed");
		t->DecodeLines(data.Load("lines"));
		if (t->id >= NextTicketId)
			NextTicketId = t->id + 1;
		return t;
	}
};

#define HELPSERV_CHAN_TYPE "HelpServChan"

class HelpServChan;
static std::vector<HelpServChan *> ExtraJoins;

class HelpServChan final
	: public Serializable
{
public:
	Anope::string queue;
	Anope::string spec;

	HelpServChan()
		: Serializable(HELPSERV_CHAN_TYPE)
	{
		ExtraJoins.push_back(this);
	}

	~HelpServChan() override
	{
		auto it = std::find(ExtraJoins.begin(), ExtraJoins.end(), this);
		if (it != ExtraJoins.end())
			ExtraJoins.erase(it);
	}

	Anope::string ChannelName() const
	{
		return ChannelNameFromSpec(spec);
	}

	static HelpServChan *Find(const Anope::string &queue, const Anope::string &chname)
	{
		for (auto *j : ExtraJoins)
		{
			if (j->queue.equals_ci(queue) && j->ChannelName().equals_ci(chname))
				return j;
		}
		return nullptr;
	}
};

class HelpServChanType final
	: public Serialize::Type
{
public:
	HelpServChanType(Module *owner)
		: Serialize::Type(HELPSERV_CHAN_TYPE, owner)
	{
	}

	void Serialize(Serializable *obj, Serialize::Data &data) const override
	{
		const auto *j = static_cast<const HelpServChan *>(obj);
		data.Store("queue", j->queue);
		data.Store("spec", j->spec);
	}

	Serializable *Unserialize(Serializable *obj, Serialize::Data &data) const override
	{
		HelpServChan *j;
		if (obj)
			j = anope_dynamic_static_cast<HelpServChan *>(obj);
		else
			j = new HelpServChan();
		j->queue = data.Load("queue");
		j->spec = data.Load("spec");
		return j;
	}
};

struct HelpAutoReply final
{
	std::vector<Anope::string> keys;
	Anope::string reply;
	bool general = false;
};

struct TriageState final
{
	Anope::string queue;
	unsigned step = 0;
	Anope::string category;
	Anope::string target;
	Anope::string channel;
	Anope::string summary;
	std::vector<Anope::string> notes;
	time_t started = 0;
	bool faq_sent = false;
};

struct FloodState final
{
	time_t last_msg = 0;
	Anope::string last_text;
	unsigned repeats = 0;
	bool warned = false;
};

class ModuleHelpServ;

static ModuleHelpServ *MeHelpServ = nullptr;

class ModuleHelpServ
	: public Module
{
	AideTicketType ticket_type;
	HelpServChanType chan_type;
	Anope::map<TriageState> triages;
	Anope::map<FloodState> floods;
	Anope::map<time_t> limit_notices;

	Anope::string staff_nick = "HelpServ";
	Anope::string staff_user = "helpserv";
	Anope::string staff_host;
	Anope::string staff_real = "Help desk";
	Anope::string staff_modes;
	Anope::string help_nick;
	Anope::string report_nick;
	Anope::string help_user;
	Anope::string report_user;
	Anope::string help_host;
	Anope::string report_host;
	Anope::string help_real;
	Anope::string report_real;
	Anope::string help_modes;
	Anope::string report_modes;
	Anope::string help_channel;
	Anope::string report_channel;
	Anope::string staff_channel;
	Anope::string log_channel;
	Anope::string help_greeting;
	bool require_account_report = false;
	unsigned min_request_len = 12;
	unsigned msg_interval = 2;
	unsigned unreg_min_online = 20;
	unsigned unreg_per_hour = 2;
	unsigned unreg_per_day = 5;
	unsigned reg_per_hour = 6;
	unsigned reg_per_day = 15;
	unsigned unreg_cooldown = 180;
	unsigned ip_per_hour = 8;
	time_t ticket_expire = 0;
	std::vector<HelpAutoReply> auto_replies;

	Reference<BotInfo> StaffBot;
	Reference<BotInfo> AideBot;
	Reference<BotInfo> ReportBot;

	class BindTimer final
		: public Timer
	{
		ModuleHelpServ *mod;
	public:
		BindTimer(ModuleHelpServ *m)
			: Timer(m, 1)
			, mod(m)
		{
		}

		bool Tick() override
		{
			mod->BindAll();
			return false;
		}
	};

	void ChanMsg(BotInfo *bi, const Anope::string &chan, const Anope::string &msg)
	{
		if (!bi || chan.empty() || msg.empty() || !IRCD)
			return;
		IRCD->SendPrivmsg(bi, chan, msg);
	}

	void NotifyStaff(const Anope::string &msg)
	{
		BotInfo *bi = StaffBot;
		ChanMsg(bi, staff_channel, msg);
		if (!log_channel.equals_ci(staff_channel))
			ChanMsg(bi, log_channel, msg);
	}

	void JoinSpec(BotInfo *bi, const Anope::string &spec)
	{
		if (!bi || spec.empty())
			return;

		size_t h = spec.find('#');
		if (h == Anope::string::npos)
			return;

		Anope::string want_modes = spec.substr(0, h);
		Anope::string chname = spec.substr(h);

		bool known = false;
		for (const auto &existing : bi->botchannels)
		{
			size_t eh = existing.find('#');
			Anope::string ename = existing.substr(eh != Anope::string::npos ? eh : 0);
			if (ename.equals_ci(chname))
			{
				known = true;
				break;
			}
		}
		if (!known)
			bi->botchannels.push_back(spec);

		bi->Join(chname);
		Channel *c = Channel::Find(chname);
		if (!c)
			return;

		c->botchannel = true;
		auto *memb = c->FindUser(bi);
		if (memb)
		{
			auto modes = memb->status.Modes();
			for (auto *mode : modes)
				c->RemoveMode(bi, mode, bi->GetUID());
		}
		for (char want_mode : want_modes)
		{
			ChannelMode *cm = ModeManager::FindChannelModeByChar(want_mode);
			if (!cm)
				cm = ModeManager::FindChannelModeByChar(ModeManager::GetStatusChar(want_mode));
			if (cm && cm->type == MODE_STATUS)
				c->SetMode(bi, cm, bi->GetUID());
		}
	}

	void PartSpec(BotInfo *bi, const Anope::string &chname)
	{
		if (!bi || chname.empty())
			return;
		for (auto it = bi->botchannels.begin(); it != bi->botchannels.end(); )
		{
			if (ChannelNameFromSpec(*it).equals_ci(chname))
				it = bi->botchannels.erase(it);
			else
				++it;
		}
		if (Channel *c = Channel::Find(chname))
			bi->Part(c);
	}

	bool JoinUserBot(BotInfo *bi, const Anope::string &chname)
	{
		if (!bi || chname.empty() || chname[0] != '#')
			return false;
		Anope::string queue = QueueFor(bi);
		if (queue.empty())
			return false;
		Anope::string spec = "@" + chname;
		if (!HelpServChan::Find(queue, chname) && !IsHomeChannel(bi, chname))
		{
			auto *j = new HelpServChan();
			j->queue = queue;
			j->spec = spec;
		}
		JoinSpec(bi, spec);
		return true;
	}

	bool PartUserBot(BotInfo *bi, const Anope::string &chname)
	{
		if (!bi || chname.empty())
			return false;
		if (IsHomeChannel(bi, chname))
			return false;
		Anope::string queue = QueueFor(bi);
		if (auto *j = HelpServChan::Find(queue, chname))
			delete j;
		PartSpec(bi, chname);
		return true;
	}

	BotInfo *EnsureBot(const Anope::string &nick, const Anope::string &user, const Anope::string &host,
		const Anope::string &real, const Anope::string &modes)
	{
		BotInfo *bi = BotInfo::Find(nick, true);
		if (!bi)
			bi = new BotInfo(nick, user, host, real, modes);
		else
		{
			if (!user.empty())
				bi->SetIdent(user);
			if (!host.empty())
				bi->host = host;
			if (!real.empty())
				bi->realname = real;
			if (!modes.empty())
				bi->botmodes = modes;
		}
		bi->conf = true;
		return bi;
	}

	void BindUserCommands(BotInfo *bi, bool report)
	{
		if (!bi)
			return;

		bi->SetCommand("HELP", "generic/help");
		bi->SetCommand("AIDE", "generic/help").hide = true;
		bi->SetCommand("WAIT", "helpserv/wait");
		bi->SetCommand("STATUS", "helpserv/wait");
		bi->SetCommand("CANCEL", "helpserv/cancel");
		bi->SetCommand("ATTENDRE", "helpserv/wait").hide = true;
		bi->SetCommand("STATUT", "helpserv/wait").hide = true;
		bi->SetCommand("ANNULER", "helpserv/cancel").hide = true;

		if (report)
		{
			bi->SetCommand("REPORT", "helpserv/report").hide = true;
			bi->SetCommand("SIGNALER", "helpserv/report").hide = true;
		}
	}

	void BindStaffCommands(BotInfo *bi)
	{
		if (!bi)
			return;

		bi->SetCommand("HELP", "generic/help");
		bi->SetCommand("AIDE", "generic/help").hide = true;

		auto bind_staff = [bi](const Anope::string &name, const Anope::string &svc, bool hide = false)
		{
			auto &ci = bi->SetCommand(name, svc);
			ci.group = "helpserv/staff";
			ci.hide = hide;
		};
		bind_staff("LIST", "helpserv/list");
		bind_staff("NEXT", "helpserv/next");
		bind_staff("PICKUP", "helpserv/pickup");
		bind_staff("SHOW", "helpserv/show");
		bind_staff("CLOSE", "helpserv/close");
		bind_staff("ADDNOTE", "helpserv/addnote");
		bind_staff("REASSIGN", "helpserv/reassign");
		bind_staff("JOIN", "helpserv/join");
		bind_staff("PART", "helpserv/part");
		bind_staff("BOTLIST", "helpserv/botlist");
		bind_staff("LISTE", "helpserv/list", true);
		bind_staff("SUIVANT", "helpserv/next", true);
		bind_staff("PRENDRE", "helpserv/pickup", true);
		bind_staff("VOIR", "helpserv/show", true);
		bind_staff("FERMER", "helpserv/close", true);
		bind_staff("NOTE", "helpserv/addnote", true);
		bind_staff("REASSIGNER", "helpserv/reassign", true);
		bind_staff("AJOUTER", "helpserv/join", true);
		bind_staff("RETIRER", "helpserv/part", true);
		bind_staff("BOTS", "helpserv/botlist", true);
	}

public:
	ModuleHelpServ(const Anope::string &modname, const Anope::string &creator)
		: Module(modname, creator, PSEUDOCLIENT | THIRD)
		, ticket_type(this)
		, chan_type(this)
	{
		MeHelpServ = this;
		SetAuthor("EntreNous");
		SetVersion("1.3");
		ModuleManager::SetPriority(this, I_OnInvite, PRIORITY_LAST);
	}

	~ModuleHelpServ() override
	{
		while (!Tickets.empty())
			delete Tickets.back();
		while (!ExtraJoins.empty())
			delete ExtraJoins.back();
		MeHelpServ = nullptr;
	}

	void BindAll()
	{
		StaffBot = EnsureBot(staff_nick, staff_user, staff_host, staff_real, staff_modes);
		AideBot = EnsureBot(help_nick, help_user, help_host, help_real, help_modes);
		ReportBot = EnsureBot(report_nick, report_user, report_host, report_real, report_modes);

		// HelpServ is the operator client: hide it from /bs botlist for non-opers
		// and block casual BotServ ASSIGN. AideMoi / SignalMoi stay normal BotServ bots.
		if (StaffBot)
			StaffBot->oper_only = true;
		if (AideBot)
			AideBot->oper_only = false;
		if (ReportBot)
			ReportBot->oper_only = false;

		BindStaffCommands(StaffBot);
		BindUserCommands(AideBot, false);
		BindUserCommands(ReportBot, true);

		// HelpServ is the operator bot: staff + logs only.
		JoinSpec(StaffBot, "@" + staff_channel);
		JoinSpec(StaffBot, "@" + log_channel);
		// User-facing bots stay on their public desks (and extra JOIN channels).
		JoinSpec(AideBot, "@" + help_channel);
		JoinSpec(ReportBot, "@" + report_channel);

		for (auto *j : ExtraJoins)
		{
			BotInfo *bi = BotForQueue(j->queue);
			if (bi)
				JoinSpec(bi, j->spec);
		}
	}

	void OnReload(Configuration::Conf &conf) override
	{
		const auto &block = conf.GetModule(this);
		const auto &help = block.GetBlock("help");
		const auto &report = block.GetBlock("report");

		staff_nick = block.Get<const Anope::string>("client", "HelpServ");
		staff_user = block.Get<const Anope::string>("user", "helpserv");
		staff_host = block.Get<const Anope::string>("host", conf.GetBlock("serverinfo").Get<const Anope::string>("name"));
		staff_real = block.Get<const Anope::string>("real", "Help desk");
		staff_modes = block.Get<const Anope::string>("modes");

		help_nick = help.Get<const Anope::string>("nick", "AideMoi");
		report_nick = report.Get<const Anope::string>("nick", "SignalMoi");
		help_user = help.Get<const Anope::string>("user", "aide");
		report_user = report.Get<const Anope::string>("user", "signal");
		help_host = help.Get<const Anope::string>("host", conf.GetBlock("serverinfo").Get<const Anope::string>("name"));
		report_host = report.Get<const Anope::string>("host", help_host);
		help_real = help.Get<const Anope::string>("real", "Help desk");
		report_real = report.Get<const Anope::string>("real", "Report desk");
		help_modes = help.Get<const Anope::string>("modes");
		report_modes = report.Get<const Anope::string>("modes");
		help_channel = help.Get<const Anope::string>("channel", "#Aide.chat");
		report_channel = report.Get<const Anope::string>("channel", "#Signalement.chat");
		staff_channel = block.Get<const Anope::string>("staff_channel", "#_BO");
		log_channel = block.Get<const Anope::string>("log_channel", "#_logs");
		help_greeting = help.Get<const Anope::string>("greeting");
		require_account_report = report.Get<bool>("require_account", "no");
		min_request_len = block.Get<unsigned>("min_request_len", "12");
		msg_interval = block.Get<unsigned>("msg_interval", "2");
		unreg_min_online = block.Get<unsigned>("unreg_min_online", "20");
		unreg_per_hour = block.Get<unsigned>("unreg_per_hour", "2");
		unreg_per_day = block.Get<unsigned>("unreg_per_day", "5");
		reg_per_hour = block.Get<unsigned>("reg_per_hour", "6");
		reg_per_day = block.Get<unsigned>("reg_per_day", "15");
		unreg_cooldown = block.Get<unsigned>("unreg_cooldown", "180");
		ip_per_hour = block.Get<unsigned>("ip_per_hour", "8");
		ticket_expire = block.Get<time_t>("ticket_expire", "7d");
		LoadAutoReplies(help);

		new BindTimer(this);
	}

	void OnPostInit() override
	{
		BindAll();
	}

	void ExpireOpenTicket(AideTicket *t)
	{
		if (!t || t->status.equals_ci(STATUS_CLOSED))
			return;
		t->status = STATUS_CLOSED;
		t->closed = Anope::CurTime;
		t->close_reason = "expired";
		t->AddLine('S', staff_nick, "expired");
		DevoiceOpener(t);
		User *opener = FindOpener(t);
		BotInfo *userbot = BotForQueue(t->queue);
		if (opener && userbot)
			opener->SendMessage(userbot, _("Your ticket \002#%u\002 has expired because it was inactive."), t->id);
		const char *fmt = Language::Translate(_("%s — expired (inactive)"));
		NotifyStaff(Anope::Format(fmt, TicketNoticePrefix(t->queue, t->id).c_str()));
	}

	void OnExpireTick() override
	{
		if (!ticket_expire || Anope::NoExpire || Anope::ReadOnly)
			return;

		for (size_t i = 0; i < Tickets.size(); )
		{
			AideTicket *t = Tickets[i];
			if (!t)
			{
				++i;
				continue;
			}

			if (t->status.equals_ci(STATUS_CLOSED))
			{
				time_t since = t->closed ? t->closed : (t->updated ? t->updated : t->opened);
				if (since && Anope::CurTime - since >= ticket_expire)
				{
					delete t;
					continue;
				}
			}
			else
			{
				time_t since = t->updated ? t->updated : t->opened;
				if (since && Anope::CurTime - since >= ticket_expire)
					ExpireOpenTicket(t);
			}
			++i;
		}
	}

	EventReturn OnPreCommand(CommandSource &source, Command *command, std::vector<Anope::string> &params) override
	{
		if (!command || !command->name.equals_ci("botserv/assign") || params.size() < 2)
			return EVENT_CONTINUE;

		BotInfo *bi = BotInfo::Find(params[1], true);
		if (!IsUserBot(bi))
			return EVENT_CONTINUE;

		source.Reply(_("Bot \002%s\002 cannot be assigned with BotServ. Use \002/msg %s JOIN %s %s\002."),
			bi->nick.c_str(),
			GetStaffBot() ? GetStaffBot()->nick.c_str() : "HelpServ",
			bi->nick.c_str(),
			params[0].c_str());
		return EVENT_STOP;
	}

	EventReturn OnPreBotAssign(User *, ChannelInfo *, BotInfo *bi) override
	{
		// Safety net for INVITE / autoassign / other callers of BotInfo::Assign.
		return IsUserBot(bi) ? EVENT_STOP : EVENT_CONTINUE;
	}

	void OnInvite(User *source, Channel *c, User *targ) override
	{
		auto *bi = dynamic_cast<BotInfo *>(targ);
		if (!source || !c || !bi || !IsUserBot(bi))
			return;
		source->SendMessage(GetStaffBot() ? GetStaffBot() : bi,
			_("Bot \002%s\002 cannot be assigned with BotServ. Use \002/msg %s JOIN %s %s\002."),
			bi->nick.c_str(),
			GetStaffBot() ? GetStaffBot()->nick.c_str() : "HelpServ",
			bi->nick.c_str(),
			c->name.c_str());
	}

	BotInfo *GetStaffBot() { return StaffBot; }
	BotInfo *GetAideBot() { return AideBot; }
	BotInfo *GetReportBot() { return ReportBot; }

	// Reference<>'s operator bool / operator* are non-const in Anope.
	bool IsStaffBot(BotInfo *bi) { return bi && bi == static_cast<BotInfo *>(StaffBot); }
	bool IsAideBot(BotInfo *bi) { return bi && bi == static_cast<BotInfo *>(AideBot); }
	bool IsReportBot(BotInfo *bi) { return bi && bi == static_cast<BotInfo *>(ReportBot); }
	bool IsUserBot(BotInfo *bi) { return IsAideBot(bi) || IsReportBot(bi); }
	bool IsOurBot(BotInfo *bi) { return IsStaffBot(bi) || IsUserBot(bi); }

	BotInfo *BotForQueue(const Anope::string &queue)
	{
		return queue.equals_ci(QUEUE_REPORT) ? GetReportBot() : GetAideBot();
	}

	Anope::string QueueFor(BotInfo *bi)
	{
		if (IsReportBot(bi))
			return QUEUE_REPORT;
		if (IsAideBot(bi))
			return QUEUE_HELP;
		return "";
	}

	bool IsHomeChannel(BotInfo *bi, const Anope::string &chname)
	{
		if (IsStaffBot(bi))
			return chname.equals_ci(staff_channel) || chname.equals_ci(log_channel);
		if (IsAideBot(bi))
			return chname.equals_ci(help_channel);
		if (IsReportBot(bi))
			return chname.equals_ci(report_channel);
		return false;
	}

	BotInfo *BotFromNick(const Anope::string &nick)
	{
		if (AideBot && nick.equals_ci(AideBot->nick))
			return AideBot;
		if (ReportBot && nick.equals_ci(ReportBot->nick))
			return ReportBot;
		if (ParseQueueName(nick).equals_ci(QUEUE_HELP))
			return AideBot;
		if (ParseQueueName(nick).equals_ci(QUEUE_REPORT))
			return ReportBot;
		return nullptr;
	}

	bool IsStaff(User *u) const
	{
		if (!u)
			return false;
		if (u->HasCommand("helpserv/helper") || u->HasCommand("helpserv/manager") || u->HasCommand("helpserv/admin")
			|| u->HasPriv("helpserv/helper") || u->HasPriv("helpserv/manager") || u->HasPriv("helpserv/admin"))
			return true;
		if (u->IsServicesOper())
			return true;

		Channel *staff = Channel::Find(staff_channel);
		if (staff && staff->FindUser(u))
			return true;

		if (HasChannelOp(u, ChannelInfo::Find(help_channel)) || HasChannelOp(u, ChannelInfo::Find(report_channel)))
			return true;

		Channel *hc = Channel::Find(help_channel);
		Channel *rc = Channel::Find(report_channel);
		if (HasChanStatus(u, hc, "OP") || HasChanStatus(u, hc, "HALFOP") || HasChanStatus(u, hc, "PROTECT") || HasChanStatus(u, hc, "OWNER"))
			return true;
		if (HasChanStatus(u, rc, "OP") || HasChanStatus(u, rc, "HALFOP") || HasChanStatus(u, rc, "PROTECT") || HasChanStatus(u, rc, "OWNER"))
			return true;
		return false;
	}

	bool IsManager(User *u) const
	{
		if (!u)
			return false;
		if (u->HasCommand("helpserv/manager") || u->HasCommand("helpserv/admin") || u->HasPriv("helpserv/manager") || u->HasPriv("helpserv/admin"))
			return true;
		auto *ci = ChannelInfo::Find(staff_channel);
		if (ci && (ci->AccessFor(u).founder || ci->AccessFor(u).HasPriv("FOUNDER") || ci->AccessFor(u).HasPriv("OWNER")))
			return true;
		return u->IsServicesOper();
	}

	bool CheckStaffSource(CommandSource &source)
	{
		if (!IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return false;
		}
		if (!IsStaffBot(source.service))
		{
			source.Reply(_("Use \002%s\002 to handle tickets."), GetStaffBot() ? GetStaffBot()->nick.c_str() : "HelpServ");
			return false;
		}
		return true;
	}

	bool IsFlood(User *u, BotInfo *bi, const Anope::string &message)
	{
		if (!u || !bi || IsStaff(u))
			return false;
		auto &f = floods[u->GetUID()];
		time_t now = Anope::CurTime;
		if (msg_interval && f.last_msg && now - f.last_msg < static_cast<time_t>(msg_interval))
		{
			if (!f.warned)
			{
				u->SendMessage(bi, _("Please slow down. Wait a few seconds between messages."));
				f.warned = true;
			}
			return true;
		}
		f.warned = false;
		if (!message.empty() && message.equals_ci(f.last_text))
		{
			++f.repeats;
			if (f.repeats >= 3)
			{
				if (f.repeats == 3)
					u->SendMessage(bi, _("Repeated messages are ignored."));
				f.last_msg = now;
				return true;
			}
		}
		else
			f.repeats = 0;
		f.last_text = message;
		f.last_msg = now;
		return false;
	}

	unsigned CountRecent(const Anope::string &queue, const Anope::string &key, const Anope::string &account, time_t since) const
	{
		unsigned n = 0;
		for (const auto *t : Tickets)
		{
			if (!t->queue.equals_ci(queue) || t->opened < since)
				continue;
			if (!account.empty() && t->opener_account.equals_ci(account))
				++n;
			else if (account.empty() && !key.empty() && t->opener_ip.equals_ci(key))
				++n;
		}
		return n;
	}

	unsigned CountRecentIp(const Anope::string &queue, const Anope::string &key, time_t since) const
	{
		if (key.empty())
			return 0;
		unsigned n = 0;
		for (const auto *t : Tickets)
		{
			if (t->queue.equals_ci(queue) && t->opened >= since && t->opener_ip.equals_ci(key))
				++n;
		}
		return n;
	}

	time_t LastClosed(const Anope::string &queue, const Anope::string &key, const Anope::string &account) const
	{
		time_t last = 0;
		for (const auto *t : Tickets)
		{
			if (!t->queue.equals_ci(queue) || !t->status.equals_ci(STATUS_CLOSED) || !t->closed)
				continue;
			bool match = false;
			if (!account.empty() && t->opener_account.equals_ci(account))
				match = true;
			if (account.empty() && !key.empty() && t->opener_ip.equals_ci(key))
				match = true;
			if (match && t->closed > last)
				last = t->closed;
		}
		return last;
	}

	void NoticeLimit(const Anope::string &queue, const User *u, const Anope::string &why)
	{
		Anope::string key = SpamKey(u);
		if (key.empty())
			key = u ? u->nick : "?";
		time_t &seen = limit_notices[queue + " " + key];
		if (seen && Anope::CurTime - seen < 600)
			return;
		seen = Anope::CurTime;
		Anope::string label = queue.equals_ci(QUEUE_REPORT)
			? Language::Translate(_("[ SIGNALEMENT ]"))
			: Language::Translate(_("[ DEMANDE D'AIDE ]"));
		const char *fmt = Language::Translate(_("%s — new ticket from %s ignored (%s)."));
		NotifyStaff(Anope::Format(fmt, label.c_str(), u ? u->nick.c_str() : "?", why.c_str()));
	}

	bool AllowNewTicket(User *u, BotInfo *bi, const Anope::string &queue)
	{
		if (!u || !bi)
			return false;
		if (IsStaff(u))
			return true;

		if (!u->Account() && unreg_min_online && u->signon && Anope::CurTime - u->signon < static_cast<time_t>(unreg_min_online))
		{
			u->SendMessage(bi, _("Please wait a few seconds after connecting before opening a ticket."));
			return false;
		}

		Anope::string key = SpamKey(u);
		Anope::string account = u->Account() ? u->Account()->display : "";
		time_t hour = Anope::CurTime - 3600;
		time_t day = Anope::CurTime - 86400;

		if (!u->Account() && unreg_cooldown)
		{
			time_t closed = LastClosed(queue, key, "");
			if (closed && Anope::CurTime - closed < static_cast<time_t>(unreg_cooldown))
			{
				u->SendMessage(bi, _("You already opened a ticket recently. Please wait before opening another."));
				NoticeLimit(queue, u, "cooldown");
				return false;
			}
		}

		unsigned hour_n = CountRecent(queue, key, account, hour);
		unsigned day_n = CountRecent(queue, key, account, day);
		unsigned hour_max = u->Account() ? reg_per_hour : unreg_per_hour;
		unsigned day_max = u->Account() ? reg_per_day : unreg_per_day;
		if ((hour_max && hour_n >= hour_max) || (day_max && day_n >= day_max))
		{
			u->SendMessage(bi, _("Too many tickets from your connection. You can still be helped: please wait, or identify to your account if you have one."));
			NoticeLimit(queue, u, "rate limit");
			return false;
		}

		if (ip_per_hour && CountRecentIp(queue, key, hour) >= ip_per_hour)
		{
			u->SendMessage(bi, _("Too many tickets from your connection. You can still be helped: please wait, or identify to your account if you have one."));
			NoticeLimit(queue, u, "IP rate limit");
			return false;
		}
		return true;
	}

	AideTicket *CreateTicket(User *u, BotInfo *bi, const Anope::string &queue, const Anope::string &summary,
		const Anope::string &category, const Anope::string &target, const Anope::string &chan,
		const std::vector<Anope::string> &notes)
	{
		auto *t = new AideTicket();
		t->id = NextTicketId++;
		t->queue = queue;
		t->status = STATUS_OPEN;
		t->opener_nick = u->nick;
		t->opener_account = u->Account() ? u->Account()->display : "";
		t->opener_host = u->GetMask();
		t->opener_uid = u->GetUID();
		t->opener_ip = SpamKey(u);
		t->target_nick = target;
		t->category = category;
		t->channel = chan;
		t->summary = summary;
		t->opened = t->updated = Anope::CurTime;
		if (!target.empty())
		{
			if (auto *na = NickAlias::Find(target))
				t->target_account = na->nc->display;
		}
		for (const auto &note : notes)
			t->AddLine('U', u->nick, note);
		if (notes.empty())
			t->AddLine('U', u->nick, summary);
		t->QueueUpdate();

		u->SendMessage(bi, _("Ticket \002#%u\002 is now open. A helper will take it as soon as possible. You can keep sending messages here; they will be added to the ticket."), t->id);

		Anope::string who = t->opener_nick;
		if (!t->opener_account.empty())
			who += " (" + t->opener_account + ")";
		const char *nfmt = Language::Translate(_("%s — %s: %s"));
		NotifyStaff(Anope::Format(nfmt, TicketNoticePrefix(queue, t->id).c_str(), who.c_str(), t->summary.c_str()));
		return t;
	}

	void AppendTicket(AideTicket *t, User *u, BotInfo *bi, const Anope::string &text)
	{
		t->AddLine('U', u->nick, text);
		u->SendMessage(bi, _("Your message has been added to ticket \002#%u\002."), t->id);

		const char *ufmt = Language::Translate(_("%s — update from %s: %s"));
		NotifyStaff(Anope::Format(ufmt, TicketNoticePrefix(t->queue, t->id).c_str(), u->nick.c_str(), text.c_str()));

		if (!t->status.equals_ci(STATUS_ASSIGNED) || t->assignee.empty())
			return;

		User *helper = User::Find(t->assignee, true);
		if (!helper)
		{
			auto *na = NickAlias::Find(t->assignee);
			if (na)
			{
				for (auto *ou : na->nc->users)
				{
					helper = ou;
					break;
				}
			}
		}
		if (helper && StaffBot)
			helper->SendMessage(StaffBot, _("Ticket \002#%u\002 new message from %s: %s"), t->id, u->nick.c_str(), text.c_str());
	}

	void AddAutoReply(const Anope::string &match, const Anope::string &reply, bool general)
	{
		HelpAutoReply item;
		item.reply = reply.replace_all_cs("\\n", "\n");
		item.general = general;
		commasepstream cs(match);
		for (Anope::string tok; cs.GetToken(tok);)
		{
			tok.trim();
			tok = FoldTriageText(tok);
			if (!tok.empty())
				item.keys.push_back(tok);
		}
		if (!item.keys.empty() && !item.reply.empty())
			auto_replies.push_back(item);
	}

	void LoadAutoReplies(const Configuration::Block &help)
	{
		auto_replies.clear();
		AddAutoReply("webchat, kiwi, kiwiirc, qwebirc, thelounge",
			"Le webchat : https://www.reseau-entrenous.fr/aide/webchat/", false);
		AddAutoReply("nickserv, identify, grouper, ghost, recover, release, enregistrer mon pseudo, enregistrer un pseudo, enregistrer mon nick",
			"Les services des pseudos NickServ : https://www.reseau-entrenous.fr/aide/nickserv/", false);
		AddAutoReply("gaya, salon personnel, salons personnels, bot des salons",
			"Le bot des salons personnels Gaya : https://www.reseau-entrenous.fr/aide/gaya/", false);
		AddAutoReply("bouncer, bnc, znc, aide serveur, fonctionnement du serveur, commandes serveur, modes serveur",
			"Comprendre le fonctionnement du serveur de tchat EntreNous : https://www.reseau-entrenous.fr/aide/aide-serveur/", false);
		AddAutoReply("utilisation, documentation, tutoriel, tuto, guide, aide en ligne, comment ca marche, comment fonctionne, site daide, reseau entrenous, entrenous.fr/aide, tchat, le tchat",
			"Besoin d'aide? Visitez https://www.reseau-entrenous.fr/aide/ pour trouver de l'aide à l'utilisation du tchat EntreNous.\n"
			"Le webchat : https://www.reseau-entrenous.fr/aide/webchat/\n"
			"Les services des pseudos NickServ : https://www.reseau-entrenous.fr/aide/nickserv/\n"
			"Le bot des salons personnels Gaya : https://www.reseau-entrenous.fr/aide/gaya/\n"
			"Comprendre le fonctionnement du serveur de tchat EntreNous : https://www.reseau-entrenous.fr/aide/aide-serveur/",
			true);

		for (const auto &[_, ab] : help.GetBlocks("auto"))
		{
			AddAutoReply(ab.Get<const Anope::string>("match"),
				ab.Get<const Anope::string>("reply"),
				ab.Get<bool>("general", "no"));
		}
	}

	void SendDocLines(User *u, BotInfo *bi, const Anope::string &text)
	{
		if (!u || !bi || text.empty())
			return;
		sepstream sep(text, '\n');
		for (Anope::string line; sep.GetToken(line);)
		{
			line.trim();
			if (!line.empty())
				u->SendMessage(bi, "%s", line.c_str());
		}
	}

	bool TryDocReply(User *u, BotInfo *bi, const Anope::string &message, TriageState &st)
	{
		if (auto_replies.empty())
			return false;

		Anope::string folded = FoldTriageText(message);
		if (folded.empty() || LooksLikeIncident(folded))
			return false;

		bool howto = LooksLikeHowTo(folded);
		const HelpAutoReply *general = nullptr;
		std::vector<const HelpAutoReply *> specific;
		for (const auto &item : auto_replies)
		{
			if (!ContainsAnyPadded(folded, item.keys))
				continue;
			if (item.general)
			{
				if (howto)
					general = &item;
				continue;
			}
			specific.push_back(&item);
		}

		if (!general && specific.empty())
		{
			if (!howto)
				return false;
			for (const auto &item : auto_replies)
			{
				if (item.general)
				{
					general = &item;
					break;
				}
			}
			if (!general)
				return false;
		}

		if (st.faq_sent)
		{
			u->SendMessage(bi, _("If the help pages are not enough, describe the exact problem (what you tried and any error) so we can open a ticket."));
			return true;
		}

		if (general)
			SendDocLines(u, bi, general->reply);
		else
		{
			for (const auto *item : specific)
				SendDocLines(u, bi, item->reply);
		}
		u->SendMessage(bi, _("If that does not solve it, describe the exact problem (what you tried and the error) and a ticket will be opened."));
		st.faq_sent = true;
		return true;
	}

	void HandleHelpDialogue(User *u, BotInfo *bi, const Anope::string &message)
	{
		if (auto *existing = AideTicket::FindOpenFor(u, QUEUE_HELP))
		{
			AppendTicket(existing, u, bi, message);
			return;
		}

		auto &st = triages[u->GetUID()];
		if (!st.started)
		{
			st.queue = QUEUE_HELP;
			st.started = Anope::CurTime;
			st.step = 0;
		}
		st.notes.push_back(message);

		if (TryDocReply(u, bi, message, st))
			return;

		if (!LooksLikeRequest(message, min_request_len))
		{
			if (IsGreeting(message) && st.step == 0)
				u->SendMessage(bi, _("Hello, I am the help bot. A ticket is only opened once we know what you need. Please describe your problem (nick, channel, connection, or something else)."));
			else
				u->SendMessage(bi, _("Could you give a little more detail so the team can help you? For example what you tried, and what error you see."));
			++st.step;
			return;
		}

		Anope::string summary = message;
		summary.trim();
		if (!AllowNewTicket(u, bi, QUEUE_HELP))
			return;
		CreateTicket(u, bi, QUEUE_HELP, summary, st.category, "", "", st.notes);
		triages.erase(u->GetUID());
	}

	void HandleReportDialogue(User *u, BotInfo *bi, const Anope::string &message)
	{
		if (auto *existing = AideTicket::FindOpenFor(u, QUEUE_REPORT))
		{
			AppendTicket(existing, u, bi, message);
			return;
		}
		if (require_account_report && !u->Account())
		{
			u->SendMessage(bi, NICK_IDENTIFY_REQUIRED);
			return;
		}

		auto &st = triages[u->GetUID()];
		if (!st.started || !st.queue.equals_ci(QUEUE_REPORT))
		{
			st = TriageState();
			st.queue = QUEUE_REPORT;
			st.started = Anope::CurTime;
			st.step = 0;
		}
		st.notes.push_back(message);

		Anope::string nick, chan;
		ExtractReportClues(message, u, nick, chan);
		if (!nick.empty() && st.target.empty())
			st.target = nick;
		if (!chan.empty() && st.channel.empty())
			st.channel = chan;

		const bool unknown = LooksLikeUnknownPerson(message);
		const bool nick_only = LooksLikeNickOnly(message);
		const bool enough = LooksLikeRequest(message, min_request_len) && !nick_only && !unknown;
		if (enough && st.summary.empty())
			st.summary = message;

		if (IsGreeting(message) && st.target.empty() && st.summary.empty())
		{
			u->SendMessage(bi, _("Hello, I take reports in private. Who are you reporting? Give their nickname if you know it, then describe what happened. Do not discuss this in a public channel."));
			++st.step;
			return;
		}

		if (st.target.empty() && !unknown)
		{
			if (enough)
				u->SendMessage(bi, _("I have noted that. Who are you reporting? Give their nickname, or say you do not know it."));
			else
				u->SendMessage(bi, _("Who are you reporting? Give their nickname. You can also describe what happened."));
			++st.step;
			return;
		}

		if (st.summary.empty() || !LooksLikeRequest(st.summary, min_request_len))
		{
			if (!enough)
			{
				if (!st.target.empty())
					u->SendMessage(bi, _("I have the nickname \002%s\002. Please describe what happened (where, when, and why you are reporting this person)."), st.target.c_str());
				else
					u->SendMessage(bi, _("Please describe what happened (where, when, and why you are reporting this person)."));
				++st.step;
				return;
			}
			st.summary = message;
		}

		if (!AllowNewTicket(u, bi, QUEUE_REPORT))
			return;
		CreateTicket(u, bi, QUEUE_REPORT, st.summary, st.category, st.target, st.channel, st.notes);
		triages.erase(u->GetUID());
		u->SendMessage(bi, _("If you have screenshots, logs, or extra details, send them now as private messages."));
	}

	void StartReport(User *u, BotInfo *bi, const Anope::string &target, const Anope::string &chan, const Anope::string &reason)
	{
		if (require_account_report && !u->Account())
		{
			u->SendMessage(bi, NICK_IDENTIFY_REQUIRED);
			return;
		}
		if (auto *existing = AideTicket::FindOpenFor(u, QUEUE_REPORT))
		{
			u->SendMessage(bi, _("You already have open report ticket \002#%u\002. Your message has been added to it."), existing->id);
			AppendTicket(existing, u, bi, reason);
			return;
		}

		if (LooksLikeRequest(reason, min_request_len))
		{
			if (!AllowNewTicket(u, bi, QUEUE_REPORT))
				return;
			std::vector<Anope::string> notes;
			notes.push_back(reason);
			CreateTicket(u, bi, QUEUE_REPORT, reason, "", target, chan, notes);
			u->SendMessage(bi, _("If you have screenshots, logs, or extra details, send them now as private messages."));
			return;
		}

		TriageState st;
		st.queue = QUEUE_REPORT;
		st.started = Anope::CurTime;
		st.target = target;
		st.channel = chan;
		st.notes.push_back(reason);
		triages[u->GetUID()] = st;
		u->SendMessage(bi, _("I have the nickname \002%s\002. Please describe what happened so I can open the ticket as close as possible to your request."), target.c_str());
	}

	bool LooksLikeCommand(User *u, BotInfo *bi, const Anope::string &message) const
	{
		Anope::string tok = FirstToken(message);
		if (tok.empty() || !bi->GetCommand(tok))
			return false;

		spacesepstream ss(message);
		Anope::string first, rest;
		ss.GetToken(first);
		rest = ss.GetRemaining();
		rest.trim();

		if (tok.equals_ci("HELP") || tok.equals_ci("AIDE"))
			return rest.empty() || rest.equals_ci("ALL") || bi->GetCommand(rest);
		if (tok.equals_ci("WAIT") || tok.equals_ci("STATUS") || tok.equals_ci("CANCEL")
			|| tok.equals_ci("ATTENDRE") || tok.equals_ci("STATUT") || tok.equals_ci("ANNULER"))
			return rest.empty();
		if (tok.equals_ci("REPORT") || tok.equals_ci("SIGNALER"))
			return true;
		return false;
	}

	EventReturn OnPreHelp(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!params.empty() || source.c || !IsOurBot(source.service))
			return EVENT_CONTINUE;
		if (IsStaffBot(source.service))
			source.Reply(_("\002%s\002 is the operator help desk. Tickets opened via \002%s\002 and \002%s\002 are delivered here and to \002%s\002 / \002%s\002.\n"
				"Helper commands: \002LIST\002, \002NEXT\002, \002PICKUP\002, \002SHOW\002, \002CLOSE\002, \002ADDNOTE\002, \002REASSIGN\002, \002JOIN\002, \002PART\002, \002BOTLIST\002."),
				StaffBot->nick.c_str(),
				AideBot ? AideBot->nick.c_str() : "AideMoi",
				ReportBot ? ReportBot->nick.c_str() : "SignalMoi",
				staff_channel.c_str(), log_channel.c_str());
		else if (IsAideBot(source.service))
			source.Reply(_("\002%s\002 is the help desk. Describe your problem in a private message; a ticket is opened only once your request is clear.\n"
				"Helpers work from \002%s\002. User commands: \002WAIT\002 (\002ATTENDRE\002), \002STATUS\002 (\002STATUT\002), \002CANCEL\002 (\002ANNULER\002)."),
				AideBot->nick.c_str(), StaffBot ? StaffBot->nick.c_str() : "HelpServ");
		else
			source.Reply(_("\002%s\002 is the report desk. Describe the report in a private message; a ticket is opened once we know who and what happened. Do not discuss it in public.\n"
				"Reports are handled by \002%s\002. User commands: \002WAIT\002, \002STATUS\002, \002CANCEL\002."),
				ReportBot->nick.c_str(), StaffBot ? StaffBot->nick.c_str() : "HelpServ");
		return EVENT_CONTINUE;
	}

	void OnPostHelp(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!params.empty() || source.c || !IsStaffBot(source.service) || !IsStaff(source.GetUser()))
			return;
		source.Reply(" ");
		source.Reply(_("French aliases: \002LISTE\002, \002SUIVANT\002, \002PRENDRE\002, \002VOIR\002, \002FERMER\002, \002NOTE\002, \002REASSIGNER\002, \002AJOUTER\002, \002RETIRER\002, \002BOTS\002.\n"
			"Queues: \002HELP\002 (\002AIDE\002) and \002REPORT\002 (\002SIGNAL\002). \002JOIN\002 / \002PART\002 add or remove \002%s\002 and \002%s\002 on channels."),
			AideBot ? AideBot->nick.c_str() : "AideMoi",
			ReportBot ? ReportBot->nick.c_str() : "SignalMoi");
	}

	EventReturn OnBotPrivmsg(User *u, BotInfo *bi, Anope::string &message, const Anope::map<Anope::string> &) override
	{
		if (!IsUserBot(bi) || !u || u->server == Me)
			return EVENT_CONTINUE;

		if (LooksLikeCommand(u, bi, message))
			return EVENT_CONTINUE;
		if (IsFlood(u, bi, message))
			return EVENT_STOP;

		if (IsAideBot(bi))
			HandleHelpDialogue(u, bi, message);
		else
			HandleReportDialogue(u, bi, message);
		return EVENT_STOP;
	}

	void OnJoinChannel(User *u, Channel *c) override
	{
		if (!u || !c || u->server == Me)
			return;
		if (IsStaff(u))
			return;

		Anope::string queue = QueueForDesk(c->name);
		if (!queue.empty())
		{
			if (AideTicket *t = AideTicket::FindOpenFor(u, queue))
			{
				RefreshOpener(t, u);
				if (t->status.equals_ci(STATUS_ASSIGNED))
					VoiceOpener(t, false);
				return;
			}
		}

		if (ReportBot && c->name.equals_ci(report_channel) && c->FindUser(ReportBot))
		{
			u->SendMessage(ReportBot, _("Welcome to %s. Send a private message to \002%s\002 to file a report. Describe what happened; no special command is needed. Do not discuss reports in public."),
				c->name.c_str(), ReportBot->nick.c_str());
			return;
		}
		if (!AideBot || !c->FindUser(AideBot))
			return;
		if (c->name.equals_ci(staff_channel) || c->name.equals_ci(log_channel))
			return;
		if (!help_greeting.empty())
			u->SendMessage(AideBot, "%s", help_greeting.c_str());
		else
			u->SendMessage(AideBot, _("Welcome to %s. Send a private message to \002%s\002 to request help. A ticket is opened only after we understand your problem."),
				c->name.c_str(), AideBot->nick.c_str());
	}

	void OnLeaveChannel(User *u, Channel *c) override
	{
		if (!u || !c || u->Quitting() || u->server == Me)
			return;

		Anope::string queue = QueueForDesk(c->name);
		if (queue.empty())
			return;

		for (auto *t : Tickets)
		{
			if (t->status.equals_ci(STATUS_CLOSED) || !t->queue.equals_ci(queue))
				continue;
			if (!t->opener_uid.equals_ci(u->GetUID()) && !t->opener_nick.equals_ci(u->nick)
				&& !(u->Account() && t->opener_account.equals_ci(u->Account()->display)))
				continue;
			t->AddLine('S', u->nick, "opener left " + c->name);
			const char *pfmt = Language::Translate(_("%s — opener %s has left %s; the ticket stays open."));
			NotifyStaff(Anope::Format(pfmt, TicketNoticePrefix(t->queue, t->id).c_str(), u->nick.c_str(), c->name.c_str()));
		}
	}

	void OnUserQuit(User *u, const Anope::string &) override
	{
		if (!u)
			return;
		triages.erase(u->GetUID());
		floods.erase(u->GetUID());
		for (auto *t : Tickets)
		{
			if (t->status.equals_ci(STATUS_CLOSED))
				continue;
			if (t->opener_uid.equals_ci(u->GetUID()))
			{
				t->AddLine('S', u->nick, "opener quit");
				const char *qfmt = Language::Translate(_("%s — opener %s has disconnected; the ticket stays open."));
				NotifyStaff(Anope::Format(qfmt, TicketNoticePrefix(t->queue, t->id).c_str(), u->nick.c_str()));
			}
		}
	}

	void OnUserNickChange(User *u, const Anope::string &) override
	{
		if (!u)
			return;
		for (auto *t : Tickets)
		{
			if (!t->status.equals_ci(STATUS_CLOSED) && t->opener_uid.equals_ci(u->GetUID()))
			{
				t->opener_nick = u->nick;
				t->QueueUpdate();
			}
		}
	}

	AideTicket *OldestUnassigned(const Anope::string &queue) const
	{
		AideTicket *best = nullptr;
		for (auto *t : Tickets)
		{
			if (!t->status.equals_ci(STATUS_OPEN))
				continue;
			if (!queue.empty() && !t->queue.equals_ci(queue))
				continue;
			if (!best || t->opened < best->opened)
				best = t;
		}
		return best;
	}

	Anope::string DeskChannelFor(const Anope::string &queue) const
	{
		return queue.equals_ci(QUEUE_REPORT) ? report_channel : help_channel;
	}

	Anope::string QueueForDesk(const Anope::string &chname) const
	{
		if (chname.equals_ci(help_channel))
			return QUEUE_HELP;
		if (chname.equals_ci(report_channel))
			return QUEUE_REPORT;
		return "";
	}

	User *FindOpener(const AideTicket *t)
	{
		if (!t)
			return nullptr;
		if (!t->opener_uid.empty())
		{
			if (User *u = User::Find(t->opener_uid))
				return u;
		}
		if (User *u = User::Find(t->opener_nick, true))
			return u;
		if (!t->opener_account.empty())
		{
			if (NickCore *nc = NickCore::Find(t->opener_account))
			{
				if (!nc->users.empty())
					return nc->users.front();
			}
		}
		return nullptr;
	}

	void RefreshOpener(AideTicket *t, User *u)
	{
		if (!t || !u)
			return;
		bool dirty = false;
		if (t->opener_nick != u->nick)
		{
			t->opener_nick = u->nick;
			dirty = true;
		}
		if (t->opener_uid != u->GetUID())
		{
			t->opener_uid = u->GetUID();
			dirty = true;
		}
		Anope::string key = SpamKey(u);
		if (!key.empty() && t->opener_ip != key)
		{
			t->opener_ip = key;
			dirty = true;
		}
		if (u->Account() && t->opener_account != u->Account()->display)
		{
			t->opener_account = u->Account()->display;
			dirty = true;
		}
		if (dirty)
			t->QueueUpdate();
	}

	void VoiceOpener(AideTicket *t, bool invite_if_absent)
	{
		if (!t || !IRCD)
			return;
		User *u = FindOpener(t);
		BotInfo *bi = BotForQueue(t->queue);
		if (!u || !bi)
			return;

		Anope::string chname = DeskChannelFor(t->queue);
		Channel *c = Channel::Find(chname);
		if (!c || !c->FindUser(bi))
			return;

		if (c->FindUser(u))
		{
			if (!HasPrivilegedChanStatus(u, c) && !HasChanStatus(u, c, "VOICE"))
				c->SetMode(bi, "VOICE", u->GetUID());
			return;
		}

		if (!invite_if_absent)
			return;

		IRCD->SendInvite(bi, c, u);
	}

	void DevoiceOpener(AideTicket *t)
	{
		if (!t)
			return;
		User *u = FindOpener(t);
		BotInfo *bi = BotForQueue(t->queue);
		if (!u || !bi)
			return;

		Channel *c = Channel::Find(DeskChannelFor(t->queue));
		if (!c || !c->FindUser(bi) || !c->FindUser(u))
			return;
		if (!HasChanStatus(u, c, "VOICE") || HasPrivilegedChanStatus(u, c) || HasChanVoiceAccess(u, c))
			return;
		c->RemoveMode(bi, "VOICE", u->GetUID());
	}

	void Assign(CommandSource &source, AideTicket *t, const Anope::string &helper)
	{
		t->assignee = helper;
		t->status = STATUS_ASSIGNED;
		t->assigned = Anope::CurTime;
		t->AddLine('S', source.GetNick(), "assigned to " + helper);
		source.Reply(_("Ticket \002#%u\002 assigned to \002%s\002."), t->id, helper.c_str());
		User *opener = FindOpener(t);
		BotInfo *userbot = BotForQueue(t->queue);
		Anope::string desk = DeskChannelFor(t->queue);
		if (opener && userbot)
		{
			Channel *c = Channel::Find(desk);
			bool on_desk = c && c->FindUser(opener);
			if (on_desk)
				opener->SendMessage(userbot, _("Helper \002%s\002 has taken your ticket \002#%u\002. You have been given voice on \002%s\002."),
					helper.c_str(), t->id, desk.c_str());
			else
				opener->SendMessage(userbot, _("Helper \002%s\002 has taken your ticket \002#%u\002. Join \002%s\002 to talk with them; you will be given voice there."),
					helper.c_str(), t->id, desk.c_str());
		}
		VoiceOpener(t, true);
		const char *afmt = Language::Translate(_("%s — assigned to %s by %s"));
		NotifyStaff(Anope::Format(afmt, TicketNoticePrefix(t->queue, t->id).c_str(), helper.c_str(), source.GetNick().c_str()));
	}

	unsigned OpenCount(const Anope::string &queue, const Anope::string &opener_nick, const Anope::string &opener_account, const Anope::string &opener_uid) const
	{
		unsigned pos = 0, total = 0;
		for (auto *t : Tickets)
		{
			if (!t->queue.equals_ci(queue) || t->status.equals_ci(STATUS_CLOSED))
				continue;
			if (!t->status.equals_ci(STATUS_ASSIGNED))
				++total;
			bool same = t->opener_uid.equals_ci(opener_uid) || t->opener_nick.equals_ci(opener_nick)
				|| (!opener_account.empty() && t->opener_account.equals_ci(opener_account));
			if (same && !t->status.equals_ci(STATUS_ASSIGNED) && pos == 0)
				pos = total;
		}
		return pos;
	}

	friend class CommandHelpServWait;
	friend class CommandHelpServCancel;
	friend class CommandHelpServReport;
	friend class CommandHelpServList;
	friend class CommandHelpServNext;
	friend class CommandHelpServPickup;
	friend class CommandHelpServShow;
	friend class CommandHelpServClose;
	friend class CommandHelpServAddNote;
	friend class CommandHelpServReassign;
	friend class CommandHelpServJoin;
	friend class CommandHelpServPart;
	friend class CommandHelpServBotList;
};

class CommandHelpServWait final
	: public Command
{
public:
	CommandHelpServWait(Module *creator)
		: Command(creator, "helpserv/wait", 0)
	{
		SetDesc(_("Show your ticket status / position in the queue"));
		AllowUnregistered(true);
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &) override
	{
		auto *mod = MeHelpServ;
		auto *u = source.GetUser();
		auto queue = mod->QueueFor(source.service);
		auto *t = AideTicket::FindOpenFor(u, queue);
		if (!t)
		{
			source.Reply(_("You do not have an open ticket with %s."), source.service->nick.c_str());
			return;
		}
		if (t->status.equals_ci(STATUS_ASSIGNED))
			source.Reply(_("Ticket \002#%u\002 is being handled by \002%s\002."), t->id, t->assignee.c_str());
		else
		{
			unsigned pos = mod->OpenCount(queue, t->opener_nick, t->opener_account, t->opener_uid);
			source.Reply(_("Ticket \002#%u\002 is waiting. Approximate position: \002%u\002."), t->id, pos ? pos : 1u);
		}
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Shows whether you have an open ticket and your position in the waiting list."));
		return true;
	}
};

class CommandHelpServCancel final
	: public Command
{
public:
	CommandHelpServCancel(Module *creator)
		: Command(creator, "helpserv/cancel", 0)
	{
		SetDesc(_("Cancel your ticket if it has not been taken yet"));
		AllowUnregistered(true);
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &) override
	{
		auto *mod = MeHelpServ;
		auto *u = source.GetUser();
		auto queue = mod->QueueFor(source.service);
		auto *t = AideTicket::FindOpenFor(u, queue);
		if (!t)
		{
			source.Reply(_("You do not have an open ticket with %s."), source.service->nick.c_str());
			return;
		}
		if (!t->status.equals_ci(STATUS_OPEN))
		{
			source.Reply(_("Ticket \002#%u\002 has already been taken and can only be closed by a helper."), t->id);
			return;
		}
		t->status = STATUS_CLOSED;
		t->closed = Anope::CurTime;
		t->close_reason = "cancelled by opener";
		t->AddLine('S', u->nick, "cancelled");
		mod->DevoiceOpener(t);
		source.Reply(_("Ticket \002#%u\002 has been cancelled."), t->id);
		const char *cfmt = Language::Translate(_("%s — cancelled by %s"));
		mod->NotifyStaff(Anope::Format(cfmt, TicketNoticePrefix(t->queue, t->id).c_str(), u->nick.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Cancels your ticket if no helper has taken it yet."));
		return true;
	}
};

class CommandHelpServReport final
	: public Command
{
public:
	CommandHelpServReport(Module *creator)
		: Command(creator, "helpserv/report", 2, 2)
	{
		SetDesc(_("File a report against a user"));
		SetSyntax(_("\037nick\037 [\037#channel\037] \037reason\037"));
		AllowUnregistered(true);
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->IsReportBot(source.service))
		{
			source.Reply(_("Use \002%s\002 to file a report."), MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
			return;
		}

		Anope::string target = params[0];
		Anope::string rest = params[1], chan, reason;
		spacesepstream ss(rest);
		Anope::string maybechan;
		if (ss.GetToken(maybechan) && !maybechan.empty() && maybechan[0] == '#')
		{
			chan = maybechan;
			reason = ss.GetRemaining();
		}
		else
			reason = rest;
		reason.trim();
		if (reason.empty())
		{
			OnSyntaxError(source, "");
			return;
		}
		MeHelpServ->StartReport(source.GetUser(), source.service, target, chan, reason);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_(
			"You can also just describe the report in a private message; a ticket opens automatically once the nickname and the facts are clear. "
			"Do not discuss the report in a public channel. Extra evidence can be sent afterwards as private messages. "
			"The reported user is not notified."
		));
		return true;
	}
};

class CommandHelpServList final
	: public Command
{
public:
	CommandHelpServList(Module *creator)
		: Command(creator, "helpserv/list", 0, 2)
	{
		SetDesc(_("List tickets"));
		SetSyntax(_("[HELP|REPORT] [UNASSIGNED | ASSIGNED | ME | ALL | CLOSED]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;

		Anope::string queue;
		Anope::string filter = "UNASSIGNED";
		for (const auto &p : params)
		{
			Anope::string q = ParseQueueName(p);
			if (!q.empty())
				queue = q;
			else
				filter = p;
		}
		Anope::string me = source.GetAccount() ? source.GetAccount()->display : source.GetNick();

		ListFormatter list(source.GetAccount());
		list.AddColumn(_("ID")).AddColumn(_("Queue")).AddColumn(_("Status")).AddColumn(_("Opener")).AddColumn(_("Helper")).AddColumn(_("Summary"));
		list.SetFlexible(_("{id} ({queue}/{status}) {opener} — {summary}"));

		for (auto *t : Tickets)
		{
			if (!queue.empty() && !t->queue.equals_ci(queue))
				continue;
			if (filter.equals_ci("UNASSIGNED") && !t->status.equals_ci(STATUS_OPEN))
				continue;
			if (filter.equals_ci("ASSIGNED") && !t->status.equals_ci(STATUS_ASSIGNED))
				continue;
			if (filter.equals_ci("CLOSED") && !t->status.equals_ci(STATUS_CLOSED))
				continue;
			if (filter.equals_ci("ME") && !t->assignee.equals_ci(me))
				continue;
			if ((filter.equals_ci("ALL") || filter.equals_ci("UNASSIGNED") || filter.equals_ci("ASSIGNED") || filter.equals_ci("ME"))
				&& t->status.equals_ci(STATUS_CLOSED) && !filter.equals_ci("ALL") && !filter.equals_ci("CLOSED"))
				continue;

			ListFormatter::ListEntry entry;
			entry["ID"] = "#" + Anope::ToString(t->id);
			entry["Queue"] = t->queue;
			entry["Status"] = t->status;
			entry["Opener"] = t->opener_nick;
			entry["Helper"] = t->assignee.empty() ? "-" : t->assignee;
			entry["Summary"] = t->summary;
			list.AddEntry(entry);
		}

		if (list.IsEmpty())
			source.Reply(_("No tickets match that filter."));
		else
			list.SendTo(source);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Lists tickets. Default: unassigned tickets from both queues. Prefix with HELP or REPORT to filter a queue."));
		return true;
	}
};

class CommandHelpServNext final
	: public Command
{
public:
	CommandHelpServNext(Module *creator)
		: Command(creator, "helpserv/next", 0, 1)
	{
		SetDesc(_("Take the oldest waiting ticket"));
		SetSyntax(_("[HELP|REPORT]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		Anope::string queue;
		if (!params.empty())
		{
			queue = ParseQueueName(params[0]);
			if (queue.empty())
			{
				this->OnSyntaxError(source, params[0]);
				return;
			}
		}
		auto *t = MeHelpServ->OldestUnassigned(queue);
		if (!t)
		{
			source.Reply(_("There are no waiting tickets."));
			return;
		}
		Anope::string helper = source.GetAccount() ? source.GetAccount()->display : source.GetNick();
		MeHelpServ->Assign(source, t, helper);
		source.Reply(_("Opener: \002%s\002 (%s) — %s"), t->opener_nick.c_str(), t->opener_host.c_str(), t->summary.c_str());
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Takes the oldest unassigned ticket. Optional HELP or REPORT selects a queue; otherwise both queues are used."));
		return true;
	}
};

class CommandHelpServPickup final
	: public Command
{
public:
	CommandHelpServPickup(Module *creator)
		: Command(creator, "helpserv/pickup", 1, 1)
	{
		SetDesc(_("Take a specific ticket"));
		SetSyntax(_("\037id\037|\037nick\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;

		AideTicket *t = nullptr;
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		if (!key.empty() && key[0] >= '0' && key[0] <= '9')
			t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t)
		{
			for (auto *cand : Tickets)
			{
				if (!cand->status.equals_ci(STATUS_CLOSED) && cand->opener_nick.equals_ci(params[0]))
				{
					t = cand;
					break;
				}
			}
		}
		if (!t)
		{
			source.Reply(_("Ticket not found."));
			return;
		}
		if (t->status.equals_ci(STATUS_CLOSED))
		{
			source.Reply(_("Ticket \002#%u\002 is closed."), t->id);
			return;
		}
		Anope::string helper = source.GetAccount() ? source.GetAccount()->display : source.GetNick();
		MeHelpServ->Assign(source, t, helper);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Takes ticket \037id\037 or the open ticket of \037nick\037."));
		return true;
	}
};

class CommandHelpServShow final
	: public Command
{
public:
	CommandHelpServShow(Module *creator)
		: Command(creator, "helpserv/show", 1, 1)
	{
		SetDesc(_("Show a ticket"));
		SetSyntax(_("\037id\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		auto *t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t)
		{
			source.Reply(_("Ticket not found."));
			return;
		}

		source.Reply(_("Ticket \002#%u\002 (%s) — %s"), t->id, t->queue.c_str(), t->status.c_str());
		source.Reply(_("Opener: %s (%s) account: %s"), t->opener_nick.c_str(), t->opener_host.c_str(),
			t->opener_account.empty() ? "-" : t->opener_account.c_str());
		if (!t->target_nick.empty())
			source.Reply(_("Target: %s"), t->target_nick.c_str());
		if (!t->channel.empty())
			source.Reply(_("Channel: %s"), t->channel.c_str());
		source.Reply(_("Opened: %s"), Anope::strftime(t->opened, source.GetAccount(), true).c_str());
		if (!t->assignee.empty())
			source.Reply(_("Helper: %s"), t->assignee.c_str());
		source.Reply(_("Summary: %s"), t->summary.c_str());
		source.Reply(_("Messages:"));
		for (const auto &line : t->lines)
		{
			const char *kind = line.type == 'N' ? "NOTE" : (line.type == 'S' ? "SYS" : "MSG");
			source.Reply("  [%s] %s %s: %s", kind, Anope::strftime(line.ts, source.GetAccount(), true).c_str(),
				line.nick.c_str(), line.text.c_str());
		}
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Shows the full ticket, including private messages and helper notes."));
		return true;
	}
};

class CommandHelpServClose final
	: public Command
{
public:
	CommandHelpServClose(Module *creator)
		: Command(creator, "helpserv/close", 1, 2)
	{
		SetDesc(_("Close a ticket"));
		SetSyntax(_("\037id\037 [\037reason\037]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		auto *t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t)
		{
			source.Reply(_("Ticket not found."));
			return;
		}
		if (t->status.equals_ci(STATUS_CLOSED))
		{
			source.Reply(_("Ticket \002#%u\002 is already closed."), t->id);
			return;
		}
		Anope::string reason = params.size() > 1 ? params[1] : "";
		t->status = STATUS_CLOSED;
		t->closed = Anope::CurTime;
		t->close_reason = reason.empty() ? "closed" : reason;
		t->AddLine('S', source.GetNick(), "closed: " + t->close_reason);
		source.Reply(_("Ticket \002#%u\002 closed."), t->id);
		MeHelpServ->DevoiceOpener(t);
		User *opener = MeHelpServ->FindOpener(t);
		if (opener)
		{
			BotInfo *userbot = MeHelpServ->BotForQueue(t->queue);
			if (userbot)
			{
				if (reason.empty())
					opener->SendMessage(userbot, _("Your ticket \002#%u\002 has been closed."), t->id);
				else
					opener->SendMessage(userbot, _("Your ticket \002#%u\002 has been closed: %s"), t->id, reason.c_str());
			}
		}
		const char *clfmt = Language::Translate(_("%s — closed by %s (%s)"));
		MeHelpServ->NotifyStaff(Anope::Format(clfmt, TicketNoticePrefix(t->queue, t->id).c_str(), source.GetNick().c_str(), t->close_reason.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Closes a ticket. The opener is notified."));
		return true;
	}
};

class CommandHelpServAddNote final
	: public Command
{
public:
	CommandHelpServAddNote(Module *creator)
		: Command(creator, "helpserv/addnote", 2, 2)
	{
		SetDesc(_("Add an internal note to a ticket"));
		SetSyntax(_("\037id\037 \037text\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		auto *t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t)
		{
			source.Reply(_("Ticket not found."));
			return;
		}
		t->AddLine('N', source.GetNick(), params[1]);
		source.Reply(_("Note added to ticket \002#%u\002."), t->id);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Adds a helper-only note. The opener does not see it."));
		return true;
	}
};

class CommandHelpServReassign final
	: public Command
{
public:
	CommandHelpServReassign(Module *creator)
		: Command(creator, "helpserv/reassign", 2, 2)
	{
		SetDesc(_("Reassign a ticket to another helper"));
		SetSyntax(_("\037id\037 \037helper\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		auto *t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t || t->status.equals_ci(STATUS_CLOSED))
		{
			source.Reply(_("Ticket not found."));
			return;
		}
		MeHelpServ->Assign(source, t, params[1]);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Gives the ticket to another helper."));
		return true;
	}
};

class CommandHelpServJoin final
	: public Command
{
public:
	CommandHelpServJoin(Module *creator)
		: Command(creator, "helpserv/join", 2, 2)
	{
		SetDesc(_("Add AideMoi or SignalMoi to a channel"));
		SetSyntax(_("\037bot\037 \037#channel\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		BotInfo *bi = MeHelpServ->BotFromNick(params[0]);
		if (!bi || MeHelpServ->IsStaffBot(bi))
		{
			source.Reply(_("Specify \002%s\002 or \002%s\002."),
				MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick.c_str() : "AideMoi",
				MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
			return;
		}
		Anope::string chan = params[1];
		if (!chan.empty() && chan[0] != '#')
			chan = "#" + chan;
		if (MeHelpServ->IsHomeChannel(bi, chan) || HelpServChan::Find(MeHelpServ->QueueFor(bi), chan))
		{
			source.Reply(_("\002%s\002 is already on \002%s\002."), bi->nick.c_str(), chan.c_str());
			return;
		}
		if (!MeHelpServ->JoinUserBot(bi, chan))
		{
			source.Reply(_("Could not add \002%s\002 to \002%s\002."), bi->nick.c_str(), chan.c_str());
			return;
		}
		source.Reply(_("\002%s\002 has been added to \002%s\002."), bi->nick.c_str(), chan.c_str());
		MeHelpServ->NotifyStaff(Anope::Format(Language::Translate(_("[%s] %s added %s to %s")),
			MeHelpServ->GetStaffBot() ? MeHelpServ->GetStaffBot()->nick.c_str() : "HelpServ",
			source.GetNick().c_str(), bi->nick.c_str(), chan.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Adds the help or report bot to a channel (with operator status on locked channels). This does not replace a BotServ assignment."));
		return true;
	}
};

class CommandHelpServPart final
	: public Command
{
public:
	CommandHelpServPart(Module *creator)
		: Command(creator, "helpserv/part", 2, 2)
	{
		SetDesc(_("Remove AideMoi or SignalMoi from a channel"));
		SetSyntax(_("\037bot\037 \037#channel\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;
		BotInfo *bi = MeHelpServ->BotFromNick(params[0]);
		if (!bi || MeHelpServ->IsStaffBot(bi))
		{
			source.Reply(_("Specify \002%s\002 or \002%s\002."),
				MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick.c_str() : "AideMoi",
				MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
			return;
		}
		Anope::string chan = params[1];
		if (!chan.empty() && chan[0] != '#')
			chan = "#" + chan;
		if (MeHelpServ->IsHomeChannel(bi, chan))
		{
			source.Reply(_("\002%s\002 cannot leave its home channel \002%s\002."), bi->nick.c_str(), chan.c_str());
			return;
		}
		if (!HelpServChan::Find(MeHelpServ->QueueFor(bi), chan))
		{
			source.Reply(_("\002%s\002 is not assigned to \002%s\002 via HelpServ."), bi->nick.c_str(), chan.c_str());
			return;
		}
		MeHelpServ->PartUserBot(bi, chan);
		source.Reply(_("\002%s\002 has been removed from \002%s\002."), bi->nick.c_str(), chan.c_str());
		MeHelpServ->NotifyStaff(Anope::Format(Language::Translate(_("[%s] %s removed %s from %s")),
			MeHelpServ->GetStaffBot() ? MeHelpServ->GetStaffBot()->nick.c_str() : "HelpServ",
			source.GetNick().c_str(), bi->nick.c_str(), chan.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Removes the help or report bot from a channel previously added with JOIN. Home channels cannot be removed."));
		return true;
	}
};

class CommandHelpServBotList final
	: public Command
{
public:
	CommandHelpServBotList(Module *creator)
		: Command(creator, "helpserv/botlist", 0)
	{
		SetDesc(_("List HelpServ client bots and their channels"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &) override
	{
		if (!MeHelpServ->CheckStaffSource(source))
			return;

		auto show = [&](BotInfo *bi, const Anope::string &role)
		{
			if (!bi)
				return;
			Anope::string chans;
			for (const auto &spec : bi->botchannels)
			{
				if (!chans.empty())
					chans += ", ";
				chans += spec;
			}
			if (chans.empty())
				chans = "-";
			source.Reply(_("\002%s\002 (%s): %s"), bi->nick.c_str(), role.c_str(), chans.c_str());
		};
		show(MeHelpServ->GetStaffBot(), _("operator desk"));
		show(MeHelpServ->GetAideBot(), _("help"));
		show(MeHelpServ->GetReportBot(), _("reports"));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Lists HelpServ, AideMoi and SignalMoi and the channels they are in."));
		return true;
	}
};

class ModuleHelpServCommands final
	: public ModuleHelpServ
{
	CommandHelpServWait cmdwait;
	CommandHelpServCancel cmdcancel;
	CommandHelpServReport cmdreport;
	CommandHelpServList cmdlist;
	CommandHelpServNext cmdnext;
	CommandHelpServPickup cmdpickup;
	CommandHelpServShow cmdshow;
	CommandHelpServClose cmdclose;
	CommandHelpServAddNote cmdnote;
	CommandHelpServReassign cmdreassign;
	CommandHelpServJoin cmdjoin;
	CommandHelpServPart cmdpart;
	CommandHelpServBotList cmdbotlist;

public:
	ModuleHelpServCommands(const Anope::string &modname, const Anope::string &creator)
		: ModuleHelpServ(modname, creator)
		, cmdwait(this)
		, cmdcancel(this)
		, cmdreport(this)
		, cmdlist(this)
		, cmdnext(this)
		, cmdpickup(this)
		, cmdshow(this)
		, cmdclose(this)
		, cmdnote(this)
		, cmdreassign(this)
		, cmdjoin(this)
		, cmdpart(this)
		, cmdbotlist(this)
	{
	}
};

MODULE_INIT(ModuleHelpServCommands)
