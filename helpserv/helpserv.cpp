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

	enum HelpLevel
	{
		HELPSERV_NONE = 0,
		HELPSERV_HELPER = 1,
		HELPSERV_SENIOR = 2,
		HELPSERV_MANAGER = 3,
		HELPSERV_ADMIN = 4
	};

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

	/* ~ owner, & admin, @ op, % halfop, + voice. Names and mode letters also work. */
	Anope::string ParseStatusPrefix(const Anope::string &in)
	{
		Anope::string t = in;
		t.trim();
		if (t.empty())
			return "";
		Anope::string low = t.lower();
		if (low.equals_ci("none") || low.equals_ci("no") || low.equals_ci("0") || low.equals_ci("-")
			|| low.equals_ci("normal") || low.equals_ci("member") || low.equals_ci("rien"))
			return "";
		if (low.equals_ci("owner") || low.equals_ci("fondateur") || low.equals_ci("founder") || low.equals_ci("q"))
			return "~";
		if (low.equals_ci("admin") || low.equals_ci("protect") || low.equals_ci("a"))
			return "&";
		if (low.equals_ci("op") || low.equals_ci("oper") || low.equals_ci("operator") || low.equals_ci("o"))
			return "@";
		if (low.equals_ci("halfop") || low.equals_ci("hop") || low.equals_ci("h"))
			return "%";
		if (low.equals_ci("voice") || low.equals_ci("v"))
			return "+";

		Anope::string out;
		for (char c : t)
		{
			if (c == '~' || c == '&' || c == '@' || c == '%' || c == '+'
				|| c == 'q' || c == 'a' || c == 'o' || c == 'h' || c == 'v'
				|| c == 'Q' || c == 'A' || c == 'O' || c == 'H' || c == 'V')
				out.push_back(c);
		}
		return out;
	}

	void SplitChanSpec(const Anope::string &in, Anope::string &prefix, Anope::string &chan)
	{
		size_t h = in.find('#');
		if (h == Anope::string::npos)
		{
			prefix = ParseStatusPrefix(in);
			chan.clear();
			return;
		}
		prefix = ParseStatusPrefix(in.substr(0, h));
		chan = in.substr(h);
	}

	ChannelMode *FindStatusMode(char want)
	{
		ChannelMode *cm = ModeManager::FindChannelModeByChar(want);
		if (!cm)
			cm = ModeManager::FindChannelModeByChar(ModeManager::GetStatusChar(want));
		if (cm && cm->type == MODE_STATUS)
			return cm;
		const char *name = nullptr;
		switch (want)
		{
			case '~':
			case 'q':
			case 'Q':
				name = "OWNER";
				break;
			case '&':
			case 'a':
			case 'A':
				name = "PROTECT";
				break;
			case '@':
			case 'o':
			case 'O':
				name = "OP";
				break;
			case '%':
			case 'h':
			case 'H':
				name = "HALFOP";
				break;
			case '+':
			case 'v':
			case 'V':
				name = "VOICE";
				break;
			default:
				break;
		}
		if (!name)
			return nullptr;
		cm = ModeManager::FindChannelModeByName(name);
		return (cm && cm->type == MODE_STATUS) ? cm : nullptr;
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

	void SplitMatchKeys(const Anope::string &match, std::vector<Anope::string> &keys)
	{
		keys.clear();
		Anope::string tmp = match;
		tmp.replace_all_cs("/", ",");
		commasepstream cs(tmp);
		for (Anope::string tok; cs.GetToken(tok);)
		{
			tok.trim();
			tok = FoldTriageText(tok);
			if (!tok.empty())
				keys.push_back(tok);
		}
	}

	bool SplitAutoAddRest(const Anope::string &in, Anope::string &match, Anope::string &reply)
	{
		size_t sep = in.find(" : ");
		size_t skip = 3;
		if (sep == Anope::string::npos)
		{
			sep = in.find(':');
			skip = 1;
		}
		if (sep == Anope::string::npos)
			return false;
		match = in.substr(0, sep);
		reply = in.substr(sep + skip);
		match.trim();
		reply.trim();
		return !match.empty() && !reply.empty();
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
			"comment faire", "comment utiliser", "comment march", "comment fonctionne",
			"cest quoi", "quest ce", "ca sert a quoi", "a quoi sert",
			"tuto", "tutoriel", "documentation", "guide",
			"lien daide", "page daide", "site daide", "ou trouver", "aide pour",
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

	void SendUserPrivmsgLine(BotInfo *source, User *target, const Anope::string &msg, const Anope::string &msgid = "")
	{
		if (!source || !target || msg.empty() || !IRCD)
			return;
		Anope::map<Anope::string> tags;
		if (!msgid.empty())
			tags["+reply"] = msgid;
		LineWrapper lw(Language::Translate(target, msg.c_str()));
		for (Anope::string line; lw.GetLine(line); )
			IRCD->SendPrivmsg(source, target->GetUID(), line, tags);
	}

	void SendUserPrivmsg(BotInfo *source, User *target, const char *fmt, ...) ATTR_FORMAT(3, 4);
	void SendUserPrivmsg(BotInfo *source, User *target, const char *fmt, ...)
	{
		if (!source || !target || !fmt)
			return;
		const char *translated_message = Language::Translate(target, fmt);
		Anope::string buf;
		ANOPE_FORMAT(fmt, translated_message, buf);
		SendUserPrivmsgLine(source, target, buf);
	}

	void SendUserNotice(BotInfo *source, User *target, const char *fmt, ...) ATTR_FORMAT(3, 4);
	void SendUserNotice(BotInfo *source, User *target, const char *fmt, ...)
	{
		if (!source || !target || !fmt || !IRCD)
			return;
		const char *translated_message = Language::Translate(target, fmt);
		Anope::string buf;
		ANOPE_FORMAT(fmt, translated_message, buf);
		LineWrapper lw(buf);
		for (Anope::string line; lw.GetLine(line); )
			IRCD->SendNotice(source, target->GetUID(), line);
	}

	class UserPrivmsgReply final
		: public CommandReply
	{
		User *target;
	public:
		explicit UserPrivmsgReply(User *t) : target(t) {}
		void SendMessage(BotInfo *source, const Anope::string &msg) override
		{
			SendUserPrivmsgLine(source, target, msg);
		}
		void SendMessage(CommandSource &source, const Anope::string &msg) override
		{
			SendUserPrivmsgLine(source.service, target, msg, source.msgid);
		}
	};

	class ChanPrivmsgReply final
		: public CommandReply
	{
		Channel *chan;
	public:
		explicit ChanPrivmsgReply(Channel *c) : chan(c) {}
		void SendMessage(BotInfo *source, const Anope::string &msg) override
		{
			if (!source || !chan || msg.empty() || !IRCD)
				return;
			LineWrapper lw(msg);
			for (Anope::string line; lw.GetLine(line); )
				IRCD->SendPrivmsg(source, chan->name, line);
		}
	};
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

namespace
{
	Anope::string TicketStatusLabel(const AideTicket *t, const NickCore *nc = nullptr)
	{
		if (!t)
			return "";
		if (t->status.equals_ci(STATUS_ASSIGNED) && !t->assignee.empty())
			return Anope::Format(Language::Translate(nc, _("waiting on %s")), t->assignee.c_str());
		if (t->status.equals_ci(STATUS_OPEN))
			return Language::Translate(nc, _("waiting"));
		if (t->status.equals_ci(STATUS_CLOSED))
		{
			if (!t->close_reason.empty() && !t->close_reason.equals_ci("closed"))
				return Anope::Format(Language::Translate(nc, _("Closed (%s)")), t->close_reason.c_str());
			return Language::Translate(nc, _("Closed"));
		}
		return t->status;
	}

	bool TicketMatchesListFilter(const AideTicket *t, const Anope::string &filter, const Anope::string &me)
	{
		if (!t)
			return false;
		if (filter.equals_ci("UNASSIGNED") || filter.equals_ci("OPEN") || filter.equals_ci("WAITING")
			|| filter.equals_ci("ATTENTE"))
			return t->status.equals_ci(STATUS_OPEN);
		if (filter.equals_ci("ASSIGNED") || filter.equals_ci("ATTRIBUE") || filter.equals_ci("TRAITEMENT"))
			return t->status.equals_ci(STATUS_ASSIGNED);
		if (filter.equals_ci("CLOSED") || filter.equals_ci("FERME"))
			return t->status.equals_ci(STATUS_CLOSED);
		if (filter.equals_ci("ME") || filter.equals_ci("MOI"))
			return t->assignee.equals_ci(me) && !t->status.equals_ci(STATUS_CLOSED);
		if (filter.equals_ci("ALL") || filter.equals_ci("TOUS"))
			return true;
		return t->status.equals_ci(STATUS_OPEN) || t->status.equals_ci(STATUS_ASSIGNED);
	}

	Anope::string TruncateSummary(Anope::string summary, size_t max_len = 80)
	{
		if (summary.length() <= max_len)
			return summary;
		if (max_len < 3)
			return summary.substr(0, max_len);
		return summary.substr(0, max_len - 3) + "...";
	}

	bool ExtractHttpUrl(const Anope::string &in, Anope::string &url)
	{
		size_t pos = in.find("https://");
		if (pos == Anope::string::npos)
			pos = in.find("http://");
		if (pos == Anope::string::npos)
			return false;
		size_t end = pos;
		while (end < in.length())
		{
			unsigned char c = static_cast<unsigned char>(in[end]);
			if (c <= 32 || c == '>' || c == '"' || c == '\'' || c == ')' || c == ']' || c == '\x01')
				break;
			++end;
		}
		url = in.substr(pos, end - pos);
		while (!url.empty())
		{
			char last = url[url.length() - 1];
			if (last != '.' && last != ',' && last != ';' && last != '!' && last != '?')
				break;
			url.erase(url.length() - 1);
		}
		return url.length() > 10;
	}

	Anope::string MediaTicketLine(const Anope::string &raw)
	{
		Anope::string plain = Anope::RemoveFormatting(raw);
		plain.trim();
		Anope::string url;
		if (!ExtractHttpUrl(plain, url))
			return plain;

		Anope::string low = plain.lower();
		Anope::string urllow = url.lower();
		if (low.find("image") != Anope::string::npos || low.find("/files/") != Anope::string::npos
			|| urllow.find(".png") != Anope::string::npos || urllow.find(".jpg") != Anope::string::npos
			|| urllow.find(".jpeg") != Anope::string::npos || urllow.find(".gif") != Anope::string::npos
			|| urllow.find(".webp") != Anope::string::npos)
			return Anope::Format(Language::Translate(_("Image: %s")), url.c_str());
		if (low.find("voice") != Anope::string::npos || low.find("audio") != Anope::string::npos
			|| urllow.find(".webm") != Anope::string::npos || urllow.find(".ogg") != Anope::string::npos
			|| urllow.find(".mp3") != Anope::string::npos || urllow.find(".m4a") != Anope::string::npos)
			return Anope::Format(Language::Translate(_("Voice note: %s")), url.c_str());
		return Anope::Format(Language::Translate(_("File: %s")), url.c_str());
	}
}

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

#define HELPSERV_AUTO_TYPE "HelpServAuto"

class HelpServAuto;
static std::vector<HelpServAuto *> CustomAutos;
static unsigned NextAutoId = 1;

class HelpServAuto final
	: public Serializable
{
public:
	unsigned id = 0;
	Anope::string queue;
	Anope::string match;
	Anope::string reply;

	HelpServAuto()
		: Serializable(HELPSERV_AUTO_TYPE)
	{
		CustomAutos.push_back(this);
	}

	~HelpServAuto() override
	{
		auto it = std::find(CustomAutos.begin(), CustomAutos.end(), this);
		if (it != CustomAutos.end())
			CustomAutos.erase(it);
	}

	std::vector<Anope::string> Keys() const
	{
		std::vector<Anope::string> keys;
		SplitMatchKeys(match, keys);
		return keys;
	}

	bool Matches(const Anope::string &folded) const
	{
		return ContainsAnyPadded(folded, Keys());
	}
};

class HelpServAutoType final
	: public Serialize::Type
{
public:
	HelpServAutoType(Module *owner)
		: Serialize::Type(HELPSERV_AUTO_TYPE, owner)
	{
	}

	void Serialize(Serializable *obj, Serialize::Data &data) const override
	{
		const auto *a = static_cast<const HelpServAuto *>(obj);
		data.Store("id", a->id);
		data.Store("queue", a->queue);
		data.Store("match", a->match);
		data.Store("reply", a->reply);
	}

	Serializable *Unserialize(Serializable *obj, Serialize::Data &data) const override
	{
		HelpServAuto *a;
		if (obj)
			a = anope_dynamic_static_cast<HelpServAuto *>(obj);
		else
			a = new HelpServAuto();
		a->id = data.Load<unsigned>("id");
		a->queue = data.Load("queue");
		a->match = data.Load("match");
		a->reply = data.Load("reply");
		if (!a->id)
			a->id = NextAutoId++;
		if (a->id >= NextAutoId)
			NextAutoId = a->id + 1;
		return a;
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
	HelpServAutoType auto_type;
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
	bool channel_commands = true;
	Anope::string staff_prefix = "@";
	Anope::string help_prefix = "@";
	Anope::string report_prefix = "@";
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
	time_t ticket_retain = 0;
	time_t ticket_reminder = 0;
	std::vector<HelpAutoReply> auto_replies;
	struct ServiceSnap final
	{
		Anope::string user, host, real, modes, channels, alias;
	};
	Anope::map<ServiceSnap> service_snaps;

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

	class ReminderTimer final
		: public Timer
	{
		ModuleHelpServ *mod;
	public:
		ReminderTimer(ModuleHelpServ *m, time_t interval)
			: Timer(m, interval)
			, mod(m)
		{
		}

		bool Tick() override
		{
			mod->RemindPendingTickets();
			return true;
		}
	};

	ReminderTimer *reminder_timer = nullptr;

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
		for (auto &existing : bi->botchannels)
		{
			size_t eh = existing.find('#');
			Anope::string ename = existing.substr(eh != Anope::string::npos ? eh : 0);
			if (ename.equals_ci(chname))
			{
				existing = spec;
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
		std::vector<ChannelMode *> wanted;
		for (char want_mode : want_modes)
		{
			if (ChannelMode *cm = FindStatusMode(want_mode))
				wanted.push_back(cm);
		}
		for (auto *cm : wanted)
			c->SetMode(bi, cm, bi->GetUID());
		memb = c->FindUser(bi);
		if (memb)
		{
			auto modes = memb->status.Modes();
			for (auto *mode : modes)
			{
				bool keep = false;
				for (auto *cm : wanted)
				{
					if (mode == cm)
					{
						keep = true;
						break;
					}
				}
				if (!keep)
					c->RemoveMode(bi, mode, bi->GetUID());
			}
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

	void EnsureHomeChannel(BotInfo *bi, const Anope::string &prefix, const Anope::string &channel)
	{
		if (!bi || channel.empty() || channel[0] != '#')
			return;
		for (const auto &existing : bi->botchannels)
		{
			if (ChannelNameFromSpec(existing).equals_ci(channel))
				return;
		}
		bi->botchannels.push_back(prefix + channel);
	}

	void ApplyBotChannels(BotInfo *bi)
	{
		if (!bi)
			return;
		const std::vector<Anope::string> specs = bi->botchannels;
		for (const auto &spec : specs)
			JoinSpec(bi, spec);
	}

	Anope::string PrefixFor(BotInfo *bi)
	{
		if (IsAideBot(bi))
			return help_prefix;
		if (IsReportBot(bi))
			return report_prefix;
		return staff_prefix;
	}

	bool JoinUserBot(BotInfo *bi, const Anope::string &chname, const Anope::string &prefix)
	{
		if (!bi || chname.empty() || chname[0] != '#')
			return false;
		Anope::string queue = QueueFor(bi);
		if (queue.empty())
			return false;
		Anope::string spec = prefix + chname;
		if (auto *existing = HelpServChan::Find(queue, chname))
			existing->spec = spec;
		else if (!IsHomeChannel(bi, chname))
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
			if (!user.empty() && !user.equals_cs(bi->GetIdent()))
				bi->SetIdent(user);
			if (!host.empty())
				bi->host = host;
			if (!modes.empty())
				bi->botmodes = modes;
			if (!real.empty() && !real.equals_cs(bi->realname))
			{
				bi->SetRealname(real);
				if (bi->introduced && IRCD)
					Uplink::Send(bi, "FNAME", bi->realname);
			}
		}
		bi->conf = true;
		return bi;
	}

	bool ModuleConfigLoaded(Configuration::Conf &conf) const
	{
		for (const auto &[_, b] : conf.GetBlocks("module"))
		{
			const auto n = b.Get<const Anope::string>("name");
			if ((n.equals_ci(this->name) || n.equals_ci("helpserv") || n.equals_ci("aideserv"))
				&& b.CountBlock("help") && b.CountBlock("report"))
				return true;
		}
		return false;
	}

	static Anope::string ExpandDefines(Configuration::Conf &conf, const Anope::string &in)
	{
		Anope::string out;
		for (size_t i = 0; i < in.length(); )
		{
			if (in[i] == '$' && i + 1 < in.length() && in[i + 1] == '{')
			{
				size_t e = in.find('}', i + 2);
				if (e == Anope::string::npos)
				{
					out.push_back(in[i++]);
					continue;
				}
				Anope::string var = in.substr(i + 2, e - (i + 2));
				Anope::string val;
				if (var.length() > 4 && var.substr(0, 4).equals_cs("env."))
				{
					if (const char *env = getenv(var.c_str() + 4))
						val = env;
				}
				else
				{
					for (const auto &[_, def] : conf.GetBlocks("define"))
					{
						if (def.Get<const Anope::string>("name") == var)
						{
							val = def.Get<const Anope::string>("value");
							break;
						}
					}
				}
				out += val;
				i = e + 1;
				continue;
			}
			out.push_back(in[i++]);
		}
		return out;
	}

	static Anope::string UnquoteValue(Anope::string v)
	{
		v.trim();
		if (!v.empty() && v[v.length() - 1] == ';')
		{
			v.erase(v.length() - 1);
			v.trim();
		}
		if (v.length() >= 2 && v[0] == '"' && v[v.length() - 1] == '"')
			return v.substr(1, v.length() - 2);
		return v;
	}

	/* Configuration::File is not CoreExport; third-party modules cannot construct it. */
	void LoadSidecarServices(Configuration::Conf &conf)
	{
		static const char *const names[] = { "helpserv.conf", "helpserv.example.conf" };
		for (const char *name : names)
		{
			const Anope::string path = Anope::ExpandConfig(name);
			if (!Anope::IsFile(path))
				continue;
			FILE *fp = fopen(path.c_str(), "r");
			if (!fp)
				continue;

			bool in_comment = false, pending_service = false, in_service = false;
			int skip_depth = 0;
			ServiceSnap snap;
			Anope::string nick;
			unsigned loaded = 0;
			char buf[1024];
			while (fgets(buf, sizeof(buf), fp))
			{
				Anope::string line;
				bool quote = false;
				for (size_t i = 0; buf[i]; ++i)
				{
					if (buf[i] == '\n' || buf[i] == '\r')
						break;
					if (in_comment)
					{
						if (buf[i] == '*' && buf[i + 1] == '/')
						{
							in_comment = false;
							++i;
						}
						continue;
					}
					if (!quote && buf[i] == '/' && buf[i + 1] == '*')
					{
						in_comment = true;
						++i;
						continue;
					}
					if (!quote && (buf[i] == '#' || (buf[i] == '/' && buf[i + 1] == '/')))
						break;
					if (buf[i] == '"')
						quote = !quote;
					line.push_back(buf[i]);
				}
				line.trim();
				if (line.empty())
					continue;

				if (skip_depth)
				{
					for (char c : line)
					{
						if (c == '{')
							++skip_depth;
						else if (c == '}' && skip_depth)
							--skip_depth;
					}
					continue;
				}

				Anope::string low = line.lower();
				if (!in_service)
				{
					size_t sp = low.find_first_of(" \t{");
					Anope::string tok = sp == Anope::string::npos ? low : low.substr(0, sp);
					if (tok.equals_ci("service"))
					{
						pending_service = true;
						if (line.find('{') != Anope::string::npos)
						{
							in_service = true;
							pending_service = false;
							snap = ServiceSnap();
							nick.clear();
						}
						continue;
					}
				}
				if (pending_service && line[0] == '{')
				{
					in_service = true;
					pending_service = false;
					snap = ServiceSnap();
					nick.clear();
					continue;
				}
				pending_service = false;
				if (!in_service)
				{
					if (line.find('{') != Anope::string::npos)
						skip_depth = 1;
					continue;
				}
				if (line[0] == '}')
				{
					if (!nick.empty())
					{
						service_snaps[nick] = snap;
						++loaded;
					}
					in_service = false;
					continue;
				}
				size_t eq = line.find('=');
				if (eq == Anope::string::npos)
					continue;
				Anope::string key = line.substr(0, eq);
				Anope::string val = ExpandDefines(conf, UnquoteValue(line.substr(eq + 1)));
				key.trim();
				if (key.equals_ci("nick"))
					nick = val;
				else if (key.equals_ci("user"))
					snap.user = val;
				else if (key.equals_ci("host"))
					snap.host = val;
				else if (key.equals_ci("real"))
					snap.real = val;
				else if (key.equals_ci("modes"))
					snap.modes = val;
				else if (key.equals_ci("channels"))
					snap.channels = val;
				else if (key.equals_ci("alias"))
					snap.alias = val;
			}
			fclose(fp);
			if (loaded)
			{
				Log(LOG_DEBUG) << "helpserv: " << name
					<< " was not included from anope.conf; read "
					<< loaded << " service{} from " << path;
				return;
			}
		}
		Log(LOG_DEBUG) << "helpserv: no helpserv.conf in " << Anope::ConfigDir
			<< " — using built-in defaults";
	}

	void LoadServiceSnaps(Configuration::Conf &conf)
	{
		service_snaps.clear();
		for (const auto &[_, s] : conf.GetBlocks("service"))
		{
			const auto nick = s.Get<const Anope::string>("nick");
			if (nick.empty())
				continue;
			ServiceSnap snap;
			snap.user = s.Get<const Anope::string>("user");
			snap.host = s.Get<const Anope::string>("host");
			snap.real = s.Get<const Anope::string>("real");
			snap.modes = s.Get<const Anope::string>("modes");
			snap.channels = s.Get<const Anope::string>("channels");
			snap.alias = s.Get<const Anope::string>("alias");
			service_snaps[nick] = snap;
		}
	}

	const Configuration::Block &FindOurModule(Configuration::Conf &conf)
	{
		const Configuration::Block *best = nullptr;
		size_t best_score = 0;
		for (const auto &[_, b] : conf.GetBlocks("module"))
		{
			const auto n = b.Get<const Anope::string>("name");
			const bool named = n.equals_ci(this->name) || n.equals_ci("helpserv") || n.equals_ci("aideserv");
			if (!named && !b.CountBlock("help") && !b.CountBlock("report"))
				continue;
			size_t score = b.GetItems().size() + b.CountBlock("help") * 50 + b.CountBlock("report") * 50;
			if (named)
				score += 10;
			if (score > best_score)
			{
				best_score = score;
				best = &b;
			}
		}
		return best ? *best : conf.GetModule(this);
	}

	void OverlayFromSnap(const Anope::string &nick, Anope::string &user, Anope::string &host,
		Anope::string &real, Anope::string &modes)
	{
		auto it = service_snaps.find(nick);
		if (it == service_snaps.end())
			return;
		const auto &s = it->second;
		if (!s.user.empty())
			user = s.user;
		if (!s.host.empty())
			host = s.host;
		if (!s.real.empty())
			real = s.real;
		if (!s.modes.empty())
			modes = s.modes;
	}

	void ApplyServiceSnap(BotInfo *bi)
	{
		if (!bi)
			return;
		auto it = service_snaps.find(bi->nick);
		if (it == service_snaps.end())
			return;
		const auto &s = it->second;
		if (!s.user.empty() && !s.user.equals_cs(bi->GetIdent()))
			bi->SetIdent(s.user);
		if (!s.host.empty())
			bi->host = s.host;
		if (!s.modes.empty())
			bi->botmodes = s.modes;
		if (!s.alias.empty())
			bi->alias = s.alias;
		if (!s.real.empty() && !s.real.equals_cs(bi->realname))
		{
			bi->SetRealname(s.real);
			if (bi->introduced && IRCD)
				Uplink::Send(bi, "FNAME", bi->realname);
		}
		commasepstream sep(s.channels);
		for (Anope::string token; sep.GetToken(token);)
		{
			token.trim();
			if (!token.empty())
				JoinSpec(bi, token);
		}
	}

	void LogAppliedConfig()
	{
		auto chans = [this](const Anope::string &nick) -> Anope::string
		{
			auto it = service_snaps.find(nick);
			return it == service_snaps.end() ? Anope::string("-") : it->second.channels;
		};
		Log(LOG_DEBUG) << "helpserv: applied " << staff_nick << " real=\"" << staff_real
			<< "\" prefix=" << (staff_prefix.empty() ? Anope::string("none") : staff_prefix)
			<< " channels=" << chans(staff_nick)
			<< "; " << help_nick << " prefix=" << (help_prefix.empty() ? Anope::string("none") : help_prefix)
			<< " channels=" << chans(help_nick)
			<< "; " << report_nick << " prefix=" << (report_prefix.empty() ? Anope::string("none") : report_prefix)
			<< " channels=" << chans(report_nick)
			<< "; service{}=" << service_snaps.size();
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

		// Orbit sends image/voice shares as CTCP ACTION. Anope otherwise
		// swallows CTCPs to bots that have no handler, so the URL never
		// reached the ticket.
		bi->ctcps["ACTION"] = [this](BotInfo *bot, User *user, const Anope::string &body)
		{
			this->HandleUserAction(bot, user, body);
		};
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
		bind_staff("REOPEN", "helpserv/reopen");
		bind_staff("ADDNOTE", "helpserv/addnote");
		bind_staff("REASSIGN", "helpserv/reassign");
		bind_staff("JOIN", "helpserv/join");
		bind_staff("PART", "helpserv/part");
		bind_staff("BOTLIST", "helpserv/botlist");
		bind_staff("AUTOADD", "helpserv/autoadd");
		bind_staff("AUTODEL", "helpserv/autodel");
		bind_staff("AUTOLIST", "helpserv/autolist");
		bind_staff("LISTE", "helpserv/list", true);
		bind_staff("SUIVANT", "helpserv/next", true);
		bind_staff("PRENDRE", "helpserv/pickup", true);
		bind_staff("VOIR", "helpserv/show", true);
		bind_staff("FERMER", "helpserv/close", true);
		bind_staff("REOUVRIR", "helpserv/reopen", true);
		bind_staff("NOTE", "helpserv/addnote", true);
		bind_staff("REASSIGNER", "helpserv/reassign", true);
		bind_staff("AJOUTER", "helpserv/join", true);
		bind_staff("RETIRER", "helpserv/part", true);
		bind_staff("BOTS", "helpserv/botlist", true);
		bind_staff("AJOUTAUTO", "helpserv/autoadd", true);
		bind_staff("SUPPRIAUTO", "helpserv/autodel", true);
		bind_staff("LISTAUTO", "helpserv/autolist", true);
	}

public:
	ModuleHelpServ(const Anope::string &modname, const Anope::string &creator)
		: Module(modname, creator, PSEUDOCLIENT | THIRD)
		, ticket_type(this)
		, chan_type(this)
		, auto_type(this)
	{
		MeHelpServ = this;
		SetAuthor("EntreNous");
		SetVersion("1.16");
		ModuleManager::SetPriority(this, I_OnInvite, PRIORITY_LAST);
	}

	~ModuleHelpServ() override
	{
		while (!Tickets.empty())
			delete Tickets.back();
		while (!ExtraJoins.empty())
			delete ExtraJoins.back();
		while (!CustomAutos.empty())
			delete CustomAutos.back();
		MeHelpServ = nullptr;
	}

	void RestartReminderTimer()
	{
		if (reminder_timer)
		{
			delete reminder_timer;
			reminder_timer = nullptr;
		}
		if (ticket_reminder > 0)
			reminder_timer = new ReminderTimer(this, ticket_reminder);
	}

	void RemindPendingTickets()
	{
		if (!ticket_reminder || Anope::ReadOnly || !IRCD)
			return;

		unsigned waiting = 0, assigned = 0;
		std::vector<AideTicket *> pending;
		for (auto *t : Tickets)
		{
			if (!t)
				continue;
			if (t->status.equals_ci(STATUS_OPEN))
			{
				++waiting;
				pending.push_back(t);
			}
			else if (t->status.equals_ci(STATUS_ASSIGNED))
			{
				++assigned;
				pending.push_back(t);
			}
		}
		if (pending.empty())
			return;

		const char *hfmt = Language::Translate(_("Reminder — %u waiting, %u being handled:"));
		NotifyStaff(Anope::Format(hfmt, waiting, assigned));

		const size_t max_show = 8;
		size_t shown = 0;
		for (auto *t : pending)
		{
			if (shown >= max_show)
				break;
			const char *lfmt = Language::Translate(_("%s — %s — %s: %s"));
			NotifyStaff(Anope::Format(lfmt, TicketNoticePrefix(t->queue, t->id).c_str(),
				TicketStatusLabel(t).c_str(), t->opener_nick.c_str(), TruncateSummary(t->summary).c_str()));
			++shown;
		}
		if (pending.size() > max_show)
		{
			const char *mfmt = Language::Translate(_("...and %u more open ticket(s). Use LIST."));
			NotifyStaff(Anope::Format(mfmt, static_cast<unsigned>(pending.size() - max_show)));
		}
	}

	void BindAll()
	{
		StaffBot = EnsureBot(staff_nick, staff_user, staff_host, staff_real, staff_modes);
		AideBot = EnsureBot(help_nick, help_user, help_host, help_real, help_modes);
		ReportBot = EnsureBot(report_nick, report_user, report_host, report_real, report_modes);
		ApplyServiceSnap(StaffBot);
		ApplyServiceSnap(AideBot);
		ApplyServiceSnap(ReportBot);

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

		// Homes only if missing: service { channels } already has prefixes and extras.
		EnsureHomeChannel(StaffBot, staff_prefix, staff_channel);
		EnsureHomeChannel(StaffBot, staff_prefix, log_channel);
		EnsureHomeChannel(AideBot, help_prefix, help_channel);
		EnsureHomeChannel(ReportBot, report_prefix, report_channel);
		ApplyBotChannels(StaffBot);
		ApplyBotChannels(AideBot);
		ApplyBotChannels(ReportBot);

		for (auto *j : ExtraJoins)
		{
			BotInfo *bi = BotForQueue(j->queue);
			if (bi)
				JoinSpec(bi, j->spec);
		}
	}

	void OnReload(Configuration::Conf &conf) override
	{
		LoadServiceSnaps(conf);
		if (!ModuleConfigLoaded(conf))
			LoadSidecarServices(conf);

		const auto &block = FindOurModule(conf);
		const auto &help = block.GetBlock("help");
		const auto &report = block.GetBlock("report");

		staff_nick = block.Get<const Anope::string>("client", "HelpServ");
		staff_user = block.Get<const Anope::string>("user", "helpserv");
		staff_host = block.Get<const Anope::string>("host", conf.GetBlock("serverinfo").Get<const Anope::string>("name"));
		staff_real = block.Get<const Anope::string>("real", "Help desk");
		staff_modes = block.Get<const Anope::string>("modes");
		OverlayFromSnap(staff_nick, staff_user, staff_host, staff_real, staff_modes);

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
		OverlayFromSnap(help_nick, help_user, help_host, help_real, help_modes);
		OverlayFromSnap(report_nick, report_user, report_host, report_real, report_modes);
		help_channel = ChannelNameFromSpec(help.Get<const Anope::string>("channel", "#Aide.chat"));
		report_channel = ChannelNameFromSpec(report.Get<const Anope::string>("channel", "#Signalement.chat"));
		staff_channel = ChannelNameFromSpec(block.Get<const Anope::string>("staff_channel", "#_BO"));
		log_channel = ChannelNameFromSpec(block.Get<const Anope::string>("log_channel", "#_logs"));
		channel_commands = block.Get<bool>("channel_commands", "yes");
		staff_prefix = ParseStatusPrefix(block.Get<const Anope::string>("staff_prefix", "@"));
		help_prefix = ParseStatusPrefix(help.Get<const Anope::string>("prefix", "@"));
		report_prefix = ParseStatusPrefix(report.Get<const Anope::string>("prefix", "@"));
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
		ticket_retain = block.Get<time_t>("ticket_retain", "0");
		ticket_reminder = block.Get<time_t>("ticket_reminder", "15m");
		if (ticket_reminder > 0 && ticket_reminder < 60)
			ticket_reminder = 60;
		RestartReminderTimer();
		LoadAutoReplies(help);
		LogAppliedConfig();

		new BindTimer(this);
	}

	void OnPostInit() override
	{
		BindAll();
	}

	void OnUplinkSync(Server *) override
	{
		ApplyServiceSnap(StaffBot);
		ApplyServiceSnap(AideBot);
		ApplyServiceSnap(ReportBot);
		ApplyBotChannels(StaffBot);
		ApplyBotChannels(AideBot);
		ApplyBotChannels(ReportBot);
		for (auto *j : ExtraJoins)
		{
			BotInfo *bi = BotForQueue(j->queue);
			if (bi)
				JoinSpec(bi, j->spec);
		}
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
			SendUserPrivmsg(userbot, opener, _("Your ticket \002#%u\002 has expired because it was inactive."), t->id);
		const char *fmt = Language::Translate(_("%s — expired (inactive)"));
		NotifyStaff(Anope::Format(fmt, TicketNoticePrefix(t->queue, t->id).c_str()));
	}

	void OnExpireTick() override
	{
		if (Anope::NoExpire || Anope::ReadOnly)
			return;
		if (!ticket_expire && !ticket_retain)
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
				if (ticket_retain)
				{
					time_t since = t->closed ? t->closed : (t->updated ? t->updated : t->opened);
					if (since && Anope::CurTime - since >= ticket_retain)
					{
						delete t;
						continue;
					}
				}
			}
			else if (ticket_expire)
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
	const Anope::string &GetStaffChannel() const { return staff_channel; }

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

	Anope::string QueueFromBotParam(const Anope::string &nick)
	{
		BotInfo *bi = BotFromNick(nick);
		if (!bi || IsStaffBot(bi))
			return "";
		return QueueFor(bi);
	}

	HelpServAuto *AddCustomAuto(const Anope::string &queue, const Anope::string &match, const Anope::string &reply)
	{
		std::vector<Anope::string> keys;
		SplitMatchKeys(match, keys);
		if (queue.empty() || keys.empty() || reply.empty())
			return nullptr;
		Anope::string text = reply;
		text.replace_all_cs("\\n", "\n");
		for (auto *a : CustomAutos)
		{
			if (a->queue.equals_ci(queue) && a->match.equals_ci(match))
			{
				a->reply = text;
				a->QueueUpdate();
				return a;
			}
		}
		auto *a = new HelpServAuto();
		a->id = NextAutoId++;
		a->queue = queue;
		a->match = match;
		a->reply = text;
		a->QueueUpdate();
		return a;
	}

	unsigned DelCustomAuto(const Anope::string &queue, const Anope::string &spec)
	{
		if (queue.empty() || spec.empty())
			return 0;
		bool by_id = true;
		for (char c : spec)
		{
			if (c < '0' || c > '9')
			{
				by_id = false;
				break;
			}
		}
		unsigned id = by_id ? Anope::TryConvert<unsigned>(spec).value_or(0) : 0;
		if (by_id && !id && spec != "0")
			by_id = false;
		Anope::string folded = FoldTriageText(spec);
		unsigned n = 0;
		for (size_t i = 0; i < CustomAutos.size(); )
		{
			HelpServAuto *a = CustomAutos[i];
			if (!a->queue.equals_ci(queue))
			{
				++i;
				continue;
			}
			bool hit = by_id && a->id == id;
			if (!hit && !by_id)
			{
				if (a->match.equals_ci(spec))
					hit = true;
				else
				{
					for (const auto &k : a->Keys())
					{
						if (k.equals_ci(folded))
						{
							hit = true;
							break;
						}
					}
				}
			}
			if (hit)
			{
				delete a;
				++n;
				continue;
			}
			++i;
		}
		return n;
	}

	bool HasHelpServPriv(User *u, const char *name) const
	{
		if (!u || !name)
			return false;
		Anope::string cmd = Anope::string("helpserv/") + name;
		return u->HasCommand(cmd) || u->HasPriv(cmd);
	}

	HelpLevel ChanServStaffLevel(User *u) const
	{
		if (!u)
			return HELPSERV_NONE;
		auto *ci = ChannelInfo::Find(staff_channel);
		if (!ci)
			return HELPSERV_NONE;
		auto ag = ci->AccessFor(u);
		if (ag.founder || ag.HasPriv("FOUNDER") || ag.HasPriv("OWNER"))
			return HELPSERV_ADMIN;
		if (ag.HasPriv("PROTECT"))
			return HELPSERV_MANAGER;
		if (ag.HasPriv("OP") || ag.HasPriv("HALFOP"))
			return HELPSERV_SENIOR;
		if (ag.HasPriv("VOICE"))
			return HELPSERV_HELPER;
		return HELPSERV_NONE;
	}

	HelpLevel OpertypeStaffLevel(User *u) const
	{
		if (!u)
			return HELPSERV_NONE;
		if (HasHelpServPriv(u, "admin"))
			return HELPSERV_ADMIN;
		if (HasHelpServPriv(u, "manager"))
			return HELPSERV_MANAGER;
		if (HasHelpServPriv(u, "helper"))
			return HELPSERV_HELPER;
		return HELPSERV_NONE;
	}

	HelpLevel StaffLevel(User *u) const
	{
		HelpLevel oper = OpertypeStaffLevel(u);
		HelpLevel chan = ChanServStaffLevel(u);
		return oper > chan ? oper : chan;
	}

	bool IsStaff(User *u) const
	{
		return StaffLevel(u) >= HELPSERV_HELPER;
	}

	bool CheckStaffBotSource(CommandSource &source)
	{
		if (!IsStaffBot(source.service))
		{
			source.Reply(_("Use \002%s\002 to handle tickets."), GetStaffBot() ? GetStaffBot()->nick.c_str() : "HelpServ");
			return false;
		}
		return true;
	}

	bool CheckStaffSource(CommandSource &source)
	{
		if (StaffLevel(source.GetUser()) < HELPSERV_HELPER)
		{
			source.Reply(ACCESS_DENIED);
			return false;
		}
		return CheckStaffBotSource(source);
	}

	bool CheckSeniorSource(CommandSource &source)
	{
		if (!CheckStaffSource(source))
			return false;
		if (StaffLevel(source.GetUser()) < HELPSERV_SENIOR)
		{
			source.Reply(_("This command requires ChanServ halfop or operator access on \002%s\002."), staff_channel.c_str());
			return false;
		}
		return true;
	}

	bool CheckManagerSource(CommandSource &source)
	{
		if (!CheckStaffSource(source))
			return false;
		if (StaffLevel(source.GetUser()) < HELPSERV_MANAGER)
		{
			source.Reply(_("This command requires ChanServ admin access on \002%s\002."), staff_channel.c_str());
			return false;
		}
		return true;
	}

	bool CheckAdminSource(CommandSource &source)
	{
		if (!CheckStaffSource(source))
			return false;
		if (StaffLevel(source.GetUser()) < HELPSERV_ADMIN)
		{
			source.Reply(_("This command requires ChanServ owner or founder access on \002%s\002."), staff_channel.c_str());
			return false;
		}
		return true;
	}

	bool StripChannelCommand(const Anope::string &msg, Anope::string &rest) const
	{
		Anope::string text = Anope::RemoveFormatting(msg);
		text.trim();
		if (text.empty() || text[0] != '!')
			return false;
		text.erase(text.begin());
		spacesepstream ss(text);
		Anope::string token;
		if (!ss.GetToken(token))
			return false;
		if (!token.equals_ci("helpserv") && !token.equals_ci("hserv")
			&& !(StaffBot && token.equals_ci(StaffBot->nick)))
			return false;
		rest = ss.GetRemaining();
		rest.trim();
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
				SendUserPrivmsg(bi, u, _("Please slow down. Wait a few seconds between messages."));
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
					SendUserPrivmsg(bi, u, _("Repeated messages are ignored."));
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
			SendUserPrivmsg(bi, u, _("Please wait a few seconds after connecting before opening a ticket."));
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
				SendUserPrivmsg(bi, u, _("You already opened a ticket recently. Please wait before opening another."));
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
			SendUserPrivmsg(bi, u, _("Too many tickets from your connection. You can still be helped: please wait, or identify to your account if you have one."));
			NoticeLimit(queue, u, "rate limit");
			return false;
		}

		if (ip_per_hour && CountRecentIp(queue, key, hour) >= ip_per_hour)
		{
			SendUserPrivmsg(bi, u, _("Too many tickets from your connection. You can still be helped: please wait, or identify to your account if you have one."));
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

		SendUserPrivmsg(bi, u, _("Ticket \002#%u\002 is now open. A helper will take it as soon as possible. You can keep sending messages here; they will be added to the ticket."), t->id);

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
		SendUserPrivmsg(bi, u, _("Your message has been added to ticket \002#%u\002."), t->id);

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

	bool TryCustomReply(User *u, BotInfo *bi, const Anope::string &message, TriageState &st)
	{
		if (!u || !bi)
			return false;
		Anope::string queue = QueueFor(bi);
		if (queue.empty() || CustomAutos.empty())
			return false;
		Anope::string folded = FoldTriageText(message);
		if (folded.empty())
			return false;
		std::vector<HelpServAuto *> hits;
		for (auto *a : CustomAutos)
		{
			if (a->queue.equals_ci(queue) && a->Matches(folded))
				hits.push_back(a);
		}
		if (hits.empty())
			return false;
		if (st.faq_sent)
		{
			SendUserPrivmsg(bi, u, _("If that page is not enough, describe the problem in a few words (what you tried, and the error)."));
			return true;
		}
		unsigned sent = 0;
		for (auto *a : hits)
		{
			SendDocLines(u, bi, a->reply);
			if (++sent >= 3)
				break;
		}
		SendUserPrivmsg(bi, u, _("If that is not it, describe the problem in a few words."));
		st.faq_sent = true;
		return true;
	}

	void LoadAutoReplies(const Configuration::Block &help)
	{
		auto_replies.clear();
		AddAutoReply("webchat, kiwi, kiwiirc, qwebirc, thelounge",
			_("Webchat: https://www.reseau-entrenous.fr/aide/webchat/"), false);
		AddAutoReply("nickserv, identify, grouper, ghost, recover, release, enregistrer mon pseudo, enregistrer un pseudo, enregistrer mon nick",
			_("Nicknames (NickServ): https://www.reseau-entrenous.fr/aide/nickserv/"), false);
		AddAutoReply("gaya, salon personnel, salons personnels, bot des salons",
			_("Personal channels (Gaya): https://www.reseau-entrenous.fr/aide/gaya/"), false);
		AddAutoReply("bouncer, bnc, znc, aide serveur, fonctionnement du serveur, commandes serveur, modes serveur",
			_("Server help: https://www.reseau-entrenous.fr/aide/aide-serveur/"), false);
		AddAutoReply("documentation, tutoriel, tuto, guide, aide en ligne, site daide, page daide",
			_("Chat help: https://www.reseau-entrenous.fr/aide/"), true);

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
				SendUserPrivmsg(bi, u, "%s", line.c_str());
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
			SendUserPrivmsg(bi, u, _("If that page is not enough, describe the problem in a few words (what you tried, and the error)."));
			return true;
		}

		if (general)
			SendDocLines(u, bi, general->reply);
		else
		{
			for (const auto *item : specific)
				SendDocLines(u, bi, item->reply);
		}
		SendUserPrivmsg(bi, u, _("If that is not it, describe the problem in a few words."));
		st.faq_sent = true;
		return true;
	}

	void HandleUserAction(BotInfo *bi, User *u, const Anope::string &body)
	{
		if (!IsUserBot(bi) || !u || u->server == Me)
			return;

		Anope::string line = MediaTicketLine(body);
		if (line.empty() || IsFlood(u, bi, line))
			return;

		Anope::string queue = QueueFor(bi);
		if (AideTicket *existing = AideTicket::FindOpenFor(u, queue))
		{
			AppendTicket(existing, u, bi, line);
			return;
		}

		auto &st = triages[u->GetUID()];
		if (!st.started)
		{
			st.queue = queue;
			st.started = Anope::CurTime;
			st.step = 0;
		}
		st.notes.push_back(line);
		SendUserPrivmsg(bi, u, _("Your file has been saved. It will be attached to the ticket once you describe the problem."));
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

		if (TryCustomReply(u, bi, message, st))
			return;
		if (TryDocReply(u, bi, message, st))
			return;

		if (!LooksLikeRequest(message, min_request_len))
		{
			if (st.step == 0)
				SendUserPrivmsg(bi, u, _("Tell me what is going wrong (what you tried, and the result). For how to use the chat: https://www.reseau-entrenous.fr/aide/"));
			else
				SendUserPrivmsg(bi, u, _("A little more detail will help: the nickname, the channel, or the exact error if you have one."));
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

	void SendReportIntro(User *u, BotInfo *bi, const Anope::string &known_nick)
	{
		if (!u || !bi)
			return;
		if (!known_nick.empty())
			SendUserPrivmsg(bi, u, _("I take reports in private. I have the nickname \002%s\002. Describe what happened (where, when, and why). Do not discuss this in a public channel."), known_nick.c_str());
		else
			SendUserPrivmsg(bi, u, _("I take reports in private. Who are you reporting? Give their nickname if you know it, then describe what happened. Do not discuss this in a public channel."));
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
			SendUserPrivmsg(bi, u, NICK_IDENTIFY_REQUIRED);
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

		if (TryCustomReply(u, bi, message, st))
			return;

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
			SendReportIntro(u, bi, "");
			++st.step;
			return;
		}

		if (st.target.empty() && !unknown)
		{
			if (enough)
				SendUserPrivmsg(bi, u, _("I have noted that. Who are you reporting? Give their nickname, or say you do not know it."));
			else if (st.step == 0)
				SendReportIntro(u, bi, "");
			else
				SendUserPrivmsg(bi, u, _("Who are you reporting? Give their nickname. You can also describe what happened."));
			++st.step;
			return;
		}

		if (st.summary.empty() || !LooksLikeRequest(st.summary, min_request_len))
		{
			if (!enough)
			{
				if (st.step == 0)
					SendReportIntro(u, bi, st.target);
				else if (!st.target.empty())
					SendUserPrivmsg(bi, u, _("Describe what happened (where, when, and why you are reporting \002%s\002)."), st.target.c_str());
				else
					SendUserPrivmsg(bi, u, _("Please describe what happened (where, when, and why)."));
				++st.step;
				return;
			}
			st.summary = message;
		}

		if (!AllowNewTicket(u, bi, QUEUE_REPORT))
			return;
		CreateTicket(u, bi, QUEUE_REPORT, st.summary, st.category, st.target, st.channel, st.notes);
		triages.erase(u->GetUID());
		SendUserPrivmsg(bi, u, _("If you have screenshots, logs, or extra details, send them now as private messages."));
	}

	void StartReport(User *u, BotInfo *bi, const Anope::string &target, const Anope::string &chan, const Anope::string &reason)
	{
		if (require_account_report && !u->Account())
		{
			SendUserPrivmsg(bi, u, NICK_IDENTIFY_REQUIRED);
			return;
		}
		if (auto *existing = AideTicket::FindOpenFor(u, QUEUE_REPORT))
		{
			SendUserPrivmsg(bi, u, _("You already have open report ticket \002#%u\002. Your message has been added to it."), existing->id);
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
			SendUserPrivmsg(bi, u, _("If you have screenshots, logs, or extra details, send them now as private messages."));
			return;
		}

		TriageState st;
		st.queue = QUEUE_REPORT;
		st.started = Anope::CurTime;
		st.target = target;
		st.channel = chan;
		st.step = 1;
		if (!reason.empty())
			st.notes.push_back(reason);
		triages[u->GetUID()] = st;
		SendReportIntro(u, bi, target);
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
			source.Reply(_("\002%s\002 is the operator help desk. Tickets opened via \002%s\002 and \002%s\002 are delivered here.\n"
				"Helper commands: \002LIST\002, \002NEXT\002, \002PICKUP\002, \002SHOW\002, \002CLOSE\002, \002REOPEN\002, \002ADDNOTE\002, \002REASSIGN\002, \002JOIN\002, \002PART\002, \002BOTLIST\002, \002AUTOADD\002, \002AUTODEL\002, \002AUTOLIST\002."),
				StaffBot->nick.c_str(),
				AideBot ? AideBot->nick.c_str() : "AideMoi",
				ReportBot ? ReportBot->nick.c_str() : "SignalMoi");
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
		source.Reply(_("French aliases: \002LISTE\002, \002SUIVANT\002, \002PRENDRE\002, \002VOIR\002, \002FERMER\002, \002REOUVRIR\002, \002NOTE\002, \002REASSIGNER\002, \002AJOUTER\002, \002RETIRER\002, \002BOTS\002, \002AJOUTAUTO\002, \002SUPPRIAUTO\002, \002LISTAUTO\002.\n"
			"Queues: \002HELP\002 (\002AIDE\002) and \002REPORT\002 (\002SIGNAL\002). \002JOIN\002 / \002PART\002 add or remove \002%s\002 and \002%s\002 on channels. \002AUTOADD\002 / \002AUTODEL\002 / \002AUTOLIST\002 manage private-message keyword replies."),
			AideBot ? AideBot->nick.c_str() : "AideMoi",
			ReportBot ? ReportBot->nick.c_str() : "SignalMoi");
		source.Reply(_("On \002%s\002 and \002%s\002 you can type \002!helpserv \037command\037\002 (or \002!hserv\002) instead of a private message."),
			staff_channel.c_str(), log_channel.c_str());
	}

	EventReturn OnBotPrivmsg(User *u, BotInfo *bi, Anope::string &message, const Anope::map<Anope::string> &tags) override
	{
		if (!IsUserBot(bi) || !u || u->server == Me)
			return EVENT_CONTINUE;

		if (LooksLikeCommand(u, bi, message))
		{
			UserPrivmsgReply reply(u);
			Anope::string msgid;
			auto it = tags.find("msgid");
			if (it != tags.end())
				msgid = it->second;
			CommandSource source(u->nick, u, u->Account(), &reply, bi, msgid);
			Command::Run(source, message);
			return EVENT_STOP;
		}
		if (IsFlood(u, bi, message))
			return EVENT_STOP;

		if (IsAideBot(bi))
			HandleHelpDialogue(u, bi, message);
		else
			HandleReportDialogue(u, bi, message);
		return EVENT_STOP;
	}

	void OnPrivmsg(User *u, Channel *c, Anope::string &msg, const Anope::map<Anope::string> &tags) override
	{
		if (!channel_commands || !u || !c || !StaffBot || msg.empty() || msg[0] == '\1')
			return;
		if (!c->name.equals_ci(staff_channel) && !c->name.equals_ci(log_channel))
			return;
		if (!c->FindUser(StaffBot) || !IsStaff(u))
			return;

		Anope::string rest;
		if (!StripChannelCommand(msg, rest))
			return;

		ChanPrivmsgReply reply(c);
		Anope::string msgid;
		auto it = tags.find("msgid");
		if (it != tags.end())
			msgid = it->second;
		CommandSource source(u->nick, u, u->Account(), &reply, StaffBot, msgid);
		if (rest.empty())
		{
			source.Reply(_("Usage: \002!helpserv \037command\037\002 or \002!hserv\002 (example: \002!helpserv LIST\002). Same commands as a private message to \002%s\002."),
				StaffBot->nick.c_str());
			return;
		}
		Command::Run(source, rest);
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
			SendUserNotice(ReportBot, u, _("Welcome to %s. Send a private message to \002%s\002 to file a report. Describe what happened; no special command is needed. Do not discuss reports in public."),
				c->name.c_str(), ReportBot->nick.c_str());
			return;
		}
		if (!AideBot || !c->FindUser(AideBot))
			return;
		if (c->name.equals_ci(staff_channel) || c->name.equals_ci(log_channel))
			return;
		if (!help_greeting.empty())
			SendUserNotice(AideBot, u, "%s", help_greeting.c_str());
		else
			SendUserNotice(AideBot, u, _("Welcome to %s. Send a private message to \002%s\002 to request help. A ticket is opened only after we understand your problem."),
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
				SendUserPrivmsg(userbot, opener, _("Helper \002%s\002 has taken your ticket \002#%u\002. You have been given voice on \002%s\002."),
					helper.c_str(), t->id, desk.c_str());
			else
				SendUserPrivmsg(userbot, opener, _("Helper \002%s\002 has taken your ticket \002#%u\002. Join \002%s\002 to talk with them; you will be given voice there."),
					helper.c_str(), t->id, desk.c_str());
		}
		VoiceOpener(t, true);
		const char *afmt = Language::Translate(_("%s — assigned to %s by %s"));
		NotifyStaff(Anope::Format(afmt, TicketNoticePrefix(t->queue, t->id).c_str(), helper.c_str(), source.GetNick().c_str()));
	}

	bool SameOpener(const AideTicket *a, const AideTicket *b) const
	{
		if (!a || !b)
			return false;
		if (!a->opener_account.empty() && a->opener_account.equals_ci(b->opener_account))
			return true;
		if (!a->opener_uid.empty() && a->opener_uid.equals_ci(b->opener_uid))
			return true;
		return a->opener_account.empty() && b->opener_account.empty() && a->opener_nick.equals_ci(b->opener_nick);
	}

	AideTicket *OtherOpenTicket(const AideTicket *t) const
	{
		if (!t)
			return nullptr;
		for (auto *other : Tickets)
		{
			if (other == t || other->status.equals_ci(STATUS_CLOSED) || !other->queue.equals_ci(t->queue))
				continue;
			if (SameOpener(t, other))
				return other;
		}
		return nullptr;
	}

	void ReopenTicket(CommandSource &source, AideTicket *t, const Anope::string &reason)
	{
		if (!t)
			return;
		AideTicket *other = OtherOpenTicket(t);
		if (other)
		{
			source.Reply(_("Cannot reopen ticket \002#%u\002: \002%s\002 already has open ticket \002#%u\002."),
				t->id, t->opener_nick.c_str(), other->id);
			return;
		}

		Anope::string helper = source.GetAccount() ? source.GetAccount()->display : source.GetNick();
		Anope::string note = reason.empty() ? "reopened" : "reopened: " + reason;
		t->status = STATUS_ASSIGNED;
		t->assignee = helper;
		t->assigned = Anope::CurTime;
		t->closed = 0;
		t->close_reason.clear();
		t->AddLine('S', source.GetNick(), note);
		source.Reply(_("Ticket \002#%u\002 reopened and assigned to \002%s\002."), t->id, helper.c_str());

		User *opener = FindOpener(t);
		BotInfo *userbot = BotForQueue(t->queue);
		Anope::string desk = DeskChannelFor(t->queue);
		if (opener && userbot)
		{
			if (reason.empty())
				SendUserPrivmsg(userbot, opener, _("Your ticket \002#%u\002 has been reopened. Helper \002%s\002 will continue with you on \002%s\002."),
					t->id, helper.c_str(), desk.c_str());
			else
				SendUserPrivmsg(userbot, opener, _("Your ticket \002#%u\002 has been reopened (%s). Helper \002%s\002 will continue with you on \002%s\002."),
					t->id, reason.c_str(), helper.c_str(), desk.c_str());
		}
		VoiceOpener(t, true);
		const char *rfmt = Language::Translate(_("%s — reopened by %s, assigned to %s"));
		NotifyStaff(Anope::Format(rfmt, TicketNoticePrefix(t->queue, t->id).c_str(), source.GetNick().c_str(), helper.c_str()));
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
	friend class CommandHelpServReopen;
	friend class CommandHelpServAddNote;
	friend class CommandHelpServReassign;
	friend class CommandHelpServJoin;
	friend class CommandHelpServPart;
	friend class CommandHelpServBotList;
	friend class CommandHelpServAutoAdd;
	friend class CommandHelpServAutoDel;
	friend class CommandHelpServAutoList;
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
		: Command(creator, "helpserv/report", 1, 2)
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
		Anope::string rest = params.size() > 1 ? params[1] : "", chan, reason;
		if (!rest.empty())
		{
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
		Anope::string filter = "ALL";
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

		auto add_ticket = [&](AideTicket *t)
		{
			ListFormatter::ListEntry entry;
			entry["ID"] = "#" + Anope::ToString(t->id);
			entry["Queue"] = t->queue;
			entry["Status"] = TicketStatusLabel(t, source.GetAccount());
			entry["Opener"] = t->opener_nick;
			entry["Helper"] = t->assignee.empty() ? "-" : t->assignee;
			entry["Summary"] = t->summary;
			list.AddEntry(entry);
		};

		for (auto *t : Tickets)
		{
			if (!queue.empty() && !t->queue.equals_ci(queue))
				continue;
			if (t->status.equals_ci(STATUS_CLOSED) || !TicketMatchesListFilter(t, filter, me))
				continue;
			add_ticket(t);
		}
		for (auto *t : Tickets)
		{
			if (!queue.empty() && !t->queue.equals_ci(queue))
				continue;
			if (!t->status.equals_ci(STATUS_CLOSED) || !TicketMatchesListFilter(t, filter, me))
				continue;
			add_ticket(t);
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
		source.Reply(_("Lists tickets. Default: waiting, assigned, and closed tickets from both queues. Assigned tickets stay listed as waiting on the helper; closed tickets stay listed as Closed. Prefix with HELP or REPORT to filter a queue. UNASSIGNED, ASSIGNED, ME, ALL, and CLOSED still work as filters."));
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
		if (!MeHelpServ->CheckSeniorSource(source))
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
		if (!MeHelpServ->CheckSeniorSource(source))
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
			source.Reply(_("Ticket \002#%u\002 is closed. Use \002REOPEN %u\002 to restore it."), t->id, t->id);
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

		source.Reply(_("Ticket \002#%u\002 (%s) — %s"), t->id, t->queue.c_str(), TicketStatusLabel(t, source.GetAccount()).c_str());
		source.Reply(_("Opener: %s (%s) account: %s"), t->opener_nick.c_str(), t->opener_host.c_str(),
			t->opener_account.empty() ? "-" : t->opener_account.c_str());
		if (!t->target_nick.empty())
			source.Reply(_("Target: %s"), t->target_nick.c_str());
		if (!t->channel.empty())
			source.Reply(_("Channel: %s"), t->channel.c_str());
		source.Reply(_("Opened: %s"), Anope::strftime(t->opened, source.GetAccount(), true).c_str());
		if (t->status.equals_ci(STATUS_CLOSED) && t->closed)
			source.Reply(_("Closed: %s (%s)"), Anope::strftime(t->closed, source.GetAccount(), true).c_str(),
				t->close_reason.empty() ? "-" : t->close_reason.c_str());
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
		source.Reply(_("Shows the full ticket, including private messages and helper notes. Closed tickets stay available here and in \002LIST CLOSED\002."));
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
					SendUserPrivmsg(userbot, opener, _("Your ticket \002#%u\002 has been closed."), t->id);
				else
					SendUserPrivmsg(userbot, opener, _("Your ticket \002#%u\002 has been closed: %s"), t->id, reason.c_str());
			}
		}
		const char *clfmt = Language::Translate(_("%s — closed by %s (%s)"));
		MeHelpServ->NotifyStaff(Anope::Format(clfmt, TicketNoticePrefix(t->queue, t->id).c_str(), source.GetNick().c_str(), t->close_reason.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Closes a ticket. The opener is notified. The ticket stays in \002LIST CLOSED\002 and can be restored with \002REOPEN\002."));
		return true;
	}
};

class CommandHelpServReopen final
	: public Command
{
public:
	CommandHelpServReopen(Module *creator)
		: Command(creator, "helpserv/reopen", 1, 2)
	{
		SetDesc(_("Reopen a closed ticket"));
		SetSyntax(_("\037id\037 [\037reason\037]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckManagerSource(source))
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
		if (!t->status.equals_ci(STATUS_CLOSED))
		{
			source.Reply(_("Ticket \002#%u\002 is not closed."), t->id);
			return;
		}
		MeHelpServ->ReopenTicket(source, t, params.size() > 1 ? params[1] : "");
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("This command requires ChanServ admin access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
		source.Reply(_("Restores a closed ticket with its history and assigns it to you. The opener is notified."));
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
		if (!MeHelpServ->CheckSeniorSource(source))
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
			source.Reply(_("Ticket \002#%u\002 is closed. Use \002REOPEN %u\002 to restore it."), t->id, t->id);
			return;
		}
		MeHelpServ->Assign(source, t, params[1]);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("This command requires ChanServ admin access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
		source.Reply(_("Gives the ticket to another helper."));
		return true;
	}
};

class CommandHelpServJoin final
	: public Command
{
public:
	CommandHelpServJoin(Module *creator)
		: Command(creator, "helpserv/join", 2, 3)
	{
		SetDesc(_("Add AideMoi or SignalMoi to a channel"));
		SetSyntax(_("\037bot\037 \037#channel\037 [\037prefix\037]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckManagerSource(source))
			return;
		BotInfo *bi = MeHelpServ->BotFromNick(params[0]);
		if (!bi || MeHelpServ->IsStaffBot(bi))
		{
			source.Reply(_("Specify \002%s\002 or \002%s\002."),
				MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick.c_str() : "AideMoi",
				MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
			return;
		}
		Anope::string prefix, chan;
		SplitChanSpec(params[1], prefix, chan);
		if (chan.empty())
		{
			if (params.size() < 3)
			{
				source.Reply(_("Specify a channel, for example \002+#channel\002 or \002#channel voice\002."));
				return;
			}
			prefix = ParseStatusPrefix(params[1]);
			chan = params[2];
		}
		else if (params.size() > 2)
			prefix = ParseStatusPrefix(params[2]);
		if (prefix.empty() && (params.size() < 3) && (params[1].empty() || params[1][0] == '#'))
			prefix = MeHelpServ->PrefixFor(bi);
		if (!chan.empty() && chan[0] != '#')
			chan = "#" + chan;
		if (MeHelpServ->IsHomeChannel(bi, chan))
		{
			source.Reply(_("\002%s\002 is already on its home channel \002%s\002. Change \002prefix\002 in helpserv.conf instead."),
				bi->nick.c_str(), chan.c_str());
			return;
		}
		bool updating = HelpServChan::Find(MeHelpServ->QueueFor(bi), chan);
		if (!MeHelpServ->JoinUserBot(bi, chan, prefix))
		{
			source.Reply(_("Could not add \002%s\002 to \002%s\002."), bi->nick.c_str(), chan.c_str());
			return;
		}
		if (updating)
			source.Reply(_("\002%s\002 status on \002%s\002 updated."), bi->nick.c_str(), chan.c_str());
		else
			source.Reply(_("\002%s\002 has been added to \002%s\002."), bi->nick.c_str(), chan.c_str());
		MeHelpServ->NotifyStaff(Anope::Format(Language::Translate(_("[%s] %s added %s to %s")),
			MeHelpServ->GetStaffBot() ? MeHelpServ->GetStaffBot()->nick.c_str() : "HelpServ",
			source.GetNick().c_str(), bi->nick.c_str(), chan.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("This command requires ChanServ admin access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
		source.Reply(_("Adds the help or report bot to a channel. Optional prefix: \002~\002 owner, \002&\002 admin, \002@\002 op, \002%%\002 halfop, \002+\002 voice, or names (admin, voice, none). Default comes from helpserv.conf. Example: \002JOIN AideMoi +#salon\002. This does not replace a BotServ assignment."));
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
		if (!MeHelpServ->CheckManagerSource(source))
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
		source.Reply(_("This command requires ChanServ admin access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
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
		if (!MeHelpServ->CheckSeniorSource(source))
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

class CommandHelpServAutoAdd final
	: public Command
{
public:
	CommandHelpServAutoAdd(Module *creator)
		: Command(creator, "helpserv/autoadd", 2, 2)
	{
		SetDesc(_("Add a keyword reply for AideMoi or SignalMoi"));
		SetSyntax(_("\037bot\037 \037keys\037 : \037reply\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckAdminSource(source))
			return;
		Anope::string queue = MeHelpServ->QueueFromBotParam(params[0]);
		if (queue.empty())
		{
			source.Reply(_("Specify \002%s\002 or \002%s\002."),
				MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick.c_str() : "AideMoi",
				MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
			return;
		}
		Anope::string match, reply;
		if (!SplitAutoAddRest(params[1], match, reply))
		{
			source.Reply(_("Use \002AUTOADD %s keys : reply\002. Separate keys with a comma or \002/\002."),
				params[0].c_str());
			return;
		}
		HelpServAuto *a = MeHelpServ->AddCustomAuto(queue, match, reply);
		if (!a)
		{
			source.Reply(_("Could not add that keyword reply."));
			return;
		}
		BotInfo *bi = MeHelpServ->BotForQueue(queue);
		source.Reply(_("Reply \002#%u\002 added for \002%s\002. Keys: \002%s\002"),
			a->id, bi ? bi->nick.c_str() : params[0].c_str(), a->match.c_str());
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("This command requires ChanServ owner or founder access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
		source.Reply(_("Adds a private-message keyword reply for \002AideMoi\002 or \002SignalMoi\002. If a user writes a matching phrase in private, the bot sends the reply instead of opening a ticket immediately. Separate several keys with a comma or \002/\002. Example: \002AUTOADD AideMoi bannis, je suis banni : Vous pouvez trouver les règles ici : https://example.invalid/\002"));
		return true;
	}
};

class CommandHelpServAutoDel final
	: public Command
{
public:
	CommandHelpServAutoDel(Module *creator)
		: Command(creator, "helpserv/autodel", 2, 2)
	{
		SetDesc(_("Delete a keyword reply for AideMoi or SignalMoi"));
		SetSyntax(_("\037bot\037 \037id\037|\037key\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckAdminSource(source))
			return;
		Anope::string queue = MeHelpServ->QueueFromBotParam(params[0]);
		if (queue.empty())
		{
			source.Reply(_("Specify \002%s\002 or \002%s\002."),
				MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick.c_str() : "AideMoi",
				MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
			return;
		}
		unsigned n = MeHelpServ->DelCustomAuto(queue, params[1]);
		if (!n)
		{
			source.Reply(_("No keyword reply matched \002%s\002 on that bot."), params[1].c_str());
			return;
		}
		source.Reply(n, _("Deleted \002%u\002 keyword reply."), _("Deleted \002%u\002 keyword replies."), n);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("This command requires ChanServ owner or founder access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
		source.Reply(_("Deletes a keyword reply. Use the id from \002AUTOLIST\002, or one of the keys. Example: \002AUTODEL AideMoi 3\002"));
		return true;
	}
};

class CommandHelpServAutoList final
	: public Command
{
public:
	CommandHelpServAutoList(Module *creator)
		: Command(creator, "helpserv/autolist", 0, 1)
	{
		SetDesc(_("List keyword replies for AideMoi and SignalMoi"));
		SetSyntax(_("[\037bot\037]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeHelpServ->CheckAdminSource(source))
			return;
		Anope::string queue;
		if (!params.empty())
		{
			queue = MeHelpServ->QueueFromBotParam(params[0]);
			if (queue.empty())
			{
				source.Reply(_("Specify \002%s\002 or \002%s\002."),
					MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick.c_str() : "AideMoi",
					MeHelpServ->GetReportBot() ? MeHelpServ->GetReportBot()->nick.c_str() : "SignalMoi");
				return;
			}
		}
		unsigned n = 0;
		auto show_reply = [&](const Anope::string &label, const Anope::string &match, const Anope::string &reply)
		{
			Anope::string preview = reply;
			preview.replace_all_cs("\n", " ");
			if (preview.length() > 80)
				preview = preview.substr(0, 77) + "...";
			source.Reply("%s — \002%s\002 → %s", label.c_str(), match.c_str(), preview.c_str());
			++n;
		};
		if (queue.empty() || queue.equals_ci(QUEUE_HELP))
		{
			for (const auto &item : MeHelpServ->auto_replies)
			{
				Anope::string keys;
				for (size_t i = 0; i < item.keys.size(); ++i)
				{
					if (i)
						keys += ", ";
					keys += item.keys[i];
				}
				if (keys.empty())
					continue;
				Anope::string label = Anope::string("\002[conf]\002 ")
					+ (MeHelpServ->GetAideBot() ? MeHelpServ->GetAideBot()->nick : "AideMoi");
				show_reply(label, keys, item.reply);
			}
		}
		for (auto *a : CustomAutos)
		{
			if (!queue.empty() && !a->queue.equals_ci(queue))
				continue;
			BotInfo *bi = MeHelpServ->BotForQueue(a->queue);
			Anope::string label = Anope::Format(_("\002#%u\002 %s"), a->id,
				bi ? bi->nick.c_str() : a->queue.c_str());
			show_reply(label, a->match, a->reply);
		}
		if (!n)
		{
			if (queue.empty())
				source.Reply(_("No keyword replies."));
			else
				source.Reply(_("No keyword replies for that bot."));
		}
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("This command requires ChanServ owner or founder access on \002%s\002."), MeHelpServ->GetStaffChannel().c_str());
		source.Reply(_("Lists keyword replies. Optional bot name (\002AideMoi\002 or \002SignalMoi\002) filters the list. Entries marked \002[conf]\002 come from helpserv.conf; numbered entries come from \002AUTOADD\002."));
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
	CommandHelpServReopen cmdreopen;
	CommandHelpServAddNote cmdnote;
	CommandHelpServReassign cmdreassign;
	CommandHelpServJoin cmdjoin;
	CommandHelpServPart cmdpart;
	CommandHelpServBotList cmdbotlist;
	CommandHelpServAutoAdd cmdautoadd;
	CommandHelpServAutoDel cmdautodel;
	CommandHelpServAutoList cmdautolist;

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
		, cmdreopen(this)
		, cmdnote(this)
		, cmdreassign(this)
		, cmdjoin(this)
		, cmdpart(this)
		, cmdbotlist(this)
		, cmdautoadd(this)
		, cmdautodel(this)
		, cmdautolist(this)
	{
	}
};

MODULE_INIT(ModuleHelpServCommands)
