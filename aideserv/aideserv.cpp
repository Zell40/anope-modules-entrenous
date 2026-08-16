// Anope IRC Services <https://www.anope.org/>
//
// SPDX-License-Identifier: GPL-2.0-only
//
// AideServ: HelpServ-style ticket queues (X3-inspired) with two bots:
// AideMoi (help) and SignalMoi (reports), sharing one ticket database.

#include "module.h"

#define AIDESERV_TICKET_TYPE "AideServTicket"

namespace
{
	const char *const QUEUE_HELP = "HELP";
	const char *const QUEUE_REPORT = "REPORT";
	const char *const STATUS_OPEN = "OPEN";
	const char *const STATUS_ASSIGNED = "ASSIGNED";
	const char *const STATUS_CLOSED = "CLOSED";

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

	bool LooksLikeRequest(const Anope::string &text)
	{
		Anope::string t = text;
		t.trim();
		if (t.length() < 12)
			return false;
		return !IsGreeting(t);
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
		: Serializable(AIDESERV_TICKET_TYPE)
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
		for (auto *t : Tickets)
		{
			if (t->queue.equals_ci(queue) && !t->status.equals_ci(STATUS_CLOSED)
				&& (t->opener_uid.equals_ci(u->GetUID())
					|| t->opener_nick.equals_ci(u->nick)
					|| (!account.empty() && t->opener_account.equals_ci(account))))
				return t;
		}
		return nullptr;
	}
};

class AideTicketType final
	: public Serialize::Type
{
public:
	AideTicketType(Module *owner)
		: Serialize::Type(AIDESERV_TICKET_TYPE, owner)
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
};

class ModuleAideServ;

static ModuleAideServ *MeAideServ = nullptr;

class ModuleAideServ
	: public Module
{
	AideTicketType ticket_type;
	Anope::map<TriageState> triages;

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
	bool require_account_report = true;
	unsigned min_request_len = 12;

	Reference<BotInfo> HelpBot;
	Reference<BotInfo> ReportBot;

	class BindTimer final
		: public Timer
	{
		ModuleAideServ *mod;
	public:
		BindTimer(ModuleAideServ *m)
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

	void NotifyStaff(BotInfo *bi, const Anope::string &msg)
	{
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

	void BindCommands(BotInfo *bi, bool report)
	{
		if (!bi)
			return;

		bi->SetCommand("HELP", "generic/help");
		bi->SetCommand("AIDE", "generic/help").hide = true;
		bi->SetCommand("WAIT", "aideserv/wait");
		bi->SetCommand("STATUS", "aideserv/wait");
		bi->SetCommand("CANCEL", "aideserv/cancel");
		bi->SetCommand("ATTENDRE", "aideserv/wait").hide = true;
		bi->SetCommand("STATUT", "aideserv/wait").hide = true;
		bi->SetCommand("ANNULER", "aideserv/cancel").hide = true;

		if (report)
		{
			bi->SetCommand("REPORT", "aideserv/report");
			bi->SetCommand("SIGNALER", "aideserv/report").hide = true;
		}

		auto bind_staff = [bi](const Anope::string &name, const Anope::string &svc, bool hide = false)
		{
			auto &ci = bi->SetCommand(name, svc);
			ci.group = "aideserv/staff";
			ci.hide = hide;
		};
		bind_staff("LIST", "aideserv/list");
		bind_staff("NEXT", "aideserv/next");
		bind_staff("PICKUP", "aideserv/pickup");
		bind_staff("SHOW", "aideserv/show");
		bind_staff("CLOSE", "aideserv/close");
		bind_staff("ADDNOTE", "aideserv/addnote");
		bind_staff("REASSIGN", "aideserv/reassign");
		bind_staff("LISTE", "aideserv/list", true);
		bind_staff("SUIVANT", "aideserv/next", true);
		bind_staff("PRENDRE", "aideserv/pickup", true);
		bind_staff("VOIR", "aideserv/show", true);
		bind_staff("FERMER", "aideserv/close", true);
		bind_staff("NOTE", "aideserv/addnote", true);
		bind_staff("REASSIGNER", "aideserv/reassign", true);
	}

public:
	ModuleAideServ(const Anope::string &modname, const Anope::string &creator)
		: Module(modname, creator, THIRD)
		, ticket_type(this)
	{
		MeAideServ = this;
		SetAuthor("EntreNous");
		SetVersion("1.0");
	}

	~ModuleAideServ() override
	{
		while (!Tickets.empty())
			delete Tickets.back();
		MeAideServ = nullptr;
	}

	void BindAll()
	{
		HelpBot = EnsureBot(help_nick, help_user, help_host, help_real, help_modes);
		ReportBot = EnsureBot(report_nick, report_user, report_host, report_real, report_modes);

		BindCommands(HelpBot, false);
		BindCommands(ReportBot, true);

		// Prefix @ so the bots get ops on locked/moderated staff channels.
		JoinSpec(HelpBot, "@" + help_channel);
		JoinSpec(HelpBot, "@" + staff_channel);
		JoinSpec(HelpBot, "@" + log_channel);
		JoinSpec(ReportBot, "@" + report_channel);
		JoinSpec(ReportBot, "@" + staff_channel);
		JoinSpec(ReportBot, "@" + log_channel);
	}

	void OnReload(Configuration::Conf &conf) override
	{
		const auto &block = conf.GetModule(this);
		const auto &help = block.GetBlock("help");
		const auto &report = block.GetBlock("report");

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
		require_account_report = report.Get<bool>("require_account", "yes");
		min_request_len = block.Get<unsigned>("min_request_len", "12");

		new BindTimer(this);
	}

	void OnPostInit() override
	{
		BindAll();
	}

	BotInfo *GetHelpBot() { return HelpBot; }
	BotInfo *GetReportBot() { return ReportBot; }

	// Reference<>'s operator bool / operator* are non-const in Anope.
	bool IsHelpBot(BotInfo *bi) { return bi && bi == static_cast<BotInfo *>(HelpBot); }
	bool IsReportBot(BotInfo *bi) { return bi && bi == static_cast<BotInfo *>(ReportBot); }
	bool IsOurBot(BotInfo *bi) { return IsHelpBot(bi) || IsReportBot(bi); }

	Anope::string QueueFor(BotInfo *bi)
	{
		return IsReportBot(bi) ? QUEUE_REPORT : QUEUE_HELP;
	}

	bool IsStaff(User *u) const
	{
		if (!u)
			return false;
		if (u->HasCommand("aideserv/helper") || u->HasCommand("aideserv/manager") || u->HasCommand("aideserv/admin")
			|| u->HasPriv("aideserv/helper") || u->HasPriv("aideserv/manager") || u->HasPriv("aideserv/admin"))
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
		if (u->HasCommand("aideserv/manager") || u->HasCommand("aideserv/admin") || u->HasPriv("aideserv/manager") || u->HasPriv("aideserv/admin"))
			return true;
		auto *ci = ChannelInfo::Find(staff_channel);
		if (ci && (ci->AccessFor(u).founder || ci->AccessFor(u).HasPriv("FOUNDER") || ci->AccessFor(u).HasPriv("OWNER")))
			return true;
		return u->IsServicesOper();
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
		const char *nfmt = Language::Translate(_("[\002%s #%u\002] %s: %s"));
		Anope::string nmsg = Anope::Format(nfmt, queue.c_str(), t->id, who.c_str(), t->summary.c_str());
		NotifyStaff(bi, nmsg);
		return t;
	}

	void AppendTicket(AideTicket *t, User *u, BotInfo *bi, const Anope::string &text)
	{
		t->AddLine('U', u->nick, text);
		u->SendMessage(bi, _("Your message has been added to ticket \002#%u\002."), t->id);

		const char *ufmt = Language::Translate(_("[\002%s #%u\002] update from %s: %s"));
		NotifyStaff(bi, Anope::Format(ufmt, t->queue.c_str(), t->id, u->nick.c_str(), text.c_str()));

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
		if (helper)
			helper->SendMessage(bi, _("Ticket \002#%u\002 new message from %s: %s"), t->id, u->nick.c_str(), text.c_str());
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

		if (IsGreeting(message) && st.step == 0)
		{
			st.step = 1;
			u->SendMessage(bi, _("Hello, I am the help bot. A ticket is only opened once we know what you need. Please describe your problem (nick, channel, connection, or something else)."));
			return;
		}

		if (!LooksLikeRequest(message) && st.step < 2)
		{
			st.step = 2;
			u->SendMessage(bi, _("Could you give a little more detail so the team can help you? For example what you tried, and what error you see."));
			return;
		}

		Anope::string summary = message;
		summary.trim();
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

		auto it = triages.find(u->GetUID());
		if (it != triages.end() && it->second.queue.equals_ci(QUEUE_REPORT))
		{
			auto &st = it->second;
			st.notes.push_back(message);
			if (!LooksLikeRequest(message) && st.summary.empty() && st.notes.size() < 3)
			{
				u->SendMessage(bi, _("Please describe what happened (where, when, and why you are reporting this person)."));
				return;
			}
			if (st.summary.empty())
				st.summary = message;
			CreateTicket(u, bi, QUEUE_REPORT, st.summary, st.category, st.target, st.channel, st.notes);
			triages.erase(u->GetUID());
			return;
		}

		u->SendMessage(bi, _("To file a report, type \002REPORT \037nick\037 [\037#channel\037] \037reason\037\002 (or \002SIGNALER\002). Do not discuss reports in public. After the ticket is open, send any evidence here by private message."));
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

		if (LooksLikeRequest(reason) && reason.length() >= min_request_len)
		{
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
		return IsStaff(u);
	}

	EventReturn OnPreHelp(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!params.empty() || source.c || !IsOurBot(source.service))
			return EVENT_CONTINUE;
		if (IsHelpBot(source.service))
			source.Reply(_("\002%s\002 is the help desk. Describe your problem in a private message; a ticket is opened only once your request is clear.\n"
				"Helpers work from \002%s\002. User commands: \002WAIT\002 (\002ATTENDRE\002), \002STATUS\002 (\002STATUT\002), \002CANCEL\002 (\002ANNULER\002)."),
				HelpBot->nick.c_str(), help_channel.c_str());
		else
			source.Reply(_("\002%s\002 is the report desk. Use \002REPORT\002 (\002SIGNALER\002) to file a report; do not discuss it in public.\n"
				"Reports are handled privately. User commands: \002WAIT\002, \002STATUS\002, \002CANCEL\002, \002REPORT\002."),
				ReportBot->nick.c_str());
		return EVENT_CONTINUE;
	}

	void OnPostHelp(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!params.empty() || source.c || !IsOurBot(source.service) || !IsStaff(source.GetUser()))
			return;
		source.Reply(" ");
		source.Reply(_("Helper commands: \002LIST\002, \002NEXT\002, \002PICKUP\002, \002SHOW\002, \002CLOSE\002, \002ADDNOTE\002, \002REASSIGN\002.\n"
			"French aliases: \002LISTE\002, \002SUIVANT\002, \002PRENDRE\002, \002VOIR\002, \002FERMER\002, \002NOTE\002, \002REASSIGNER\002."));
	}

	EventReturn OnBotPrivmsg(User *u, BotInfo *bi, Anope::string &message, const Anope::map<Anope::string> &) override
	{
		if (!IsOurBot(bi) || !u || u->server == Me)
			return EVENT_CONTINUE;

		if (LooksLikeCommand(u, bi, message))
			return EVENT_CONTINUE;

		if (IsHelpBot(bi))
			HandleHelpDialogue(u, bi, message);
		else
			HandleReportDialogue(u, bi, message);
		return EVENT_STOP;
	}

	void OnJoinChannel(User *u, Channel *c) override
	{
		if (!u || !c || u->server == Me || !HelpBot)
			return;
		if (!c->name.equals_ci(help_channel))
			return;
		if (IsStaff(u))
			return;
		if (!help_greeting.empty())
			u->SendMessage(HelpBot, "%s", help_greeting.c_str());
		else
			u->SendMessage(HelpBot, _("Welcome to %s. Send a private message to \002%s\002 to request help. A ticket is opened only after we understand your problem."),
				help_channel.c_str(), HelpBot->nick.c_str());
	}

	void OnUserQuit(User *u, const Anope::string &) override
	{
		if (!u)
			return;
		triages.erase(u->GetUID());
		for (auto *t : Tickets)
		{
			if (t->status.equals_ci(STATUS_CLOSED))
				continue;
			if (t->opener_uid.equals_ci(u->GetUID()))
			{
				t->AddLine('S', u->nick, "opener quit");
				const char *qfmt = Language::Translate(_("[\002%s #%u\002] opener %s has disconnected; the ticket stays open."));
				NotifyStaff(t->queue.equals_ci(QUEUE_REPORT) ? ReportBot : HelpBot,
					Anope::Format(qfmt, t->queue.c_str(), t->id, u->nick.c_str()));
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
			if (!t->queue.equals_ci(queue) || !t->status.equals_ci(STATUS_OPEN))
				continue;
			if (!best || t->opened < best->opened)
				best = t;
		}
		return best;
	}

	void Assign(CommandSource &source, AideTicket *t, const Anope::string &helper)
	{
		t->assignee = helper;
		t->status = STATUS_ASSIGNED;
		t->assigned = Anope::CurTime;
		t->AddLine('S', source.GetNick(), "assigned to " + helper);
		source.Reply(_("Ticket \002#%u\002 assigned to \002%s\002."), t->id, helper.c_str());
		User *opener = User::Find(t->opener_nick, true);
		if (opener)
			opener->SendMessage(source.service, _("Helper \002%s\002 has taken your ticket \002#%u\002."), helper.c_str(), t->id);
		const char *afmt = Language::Translate(_("[\002%s #%u\002] assigned to %s by %s"));
		NotifyStaff(source.service, Anope::Format(afmt, t->queue.c_str(), t->id, helper.c_str(), source.GetNick().c_str()));
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

	friend class CommandAideServWait;
	friend class CommandAideServCancel;
	friend class CommandAideServReport;
	friend class CommandAideServList;
	friend class CommandAideServNext;
	friend class CommandAideServPickup;
	friend class CommandAideServShow;
	friend class CommandAideServClose;
	friend class CommandAideServAddNote;
	friend class CommandAideServReassign;
};

class CommandAideServWait final
	: public Command
{
public:
	CommandAideServWait(Module *creator)
		: Command(creator, "aideserv/wait", 0)
	{
		SetDesc(_("Show your ticket status / position in the queue"));
		AllowUnregistered(true);
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &) override
	{
		auto *mod = MeAideServ;
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

class CommandAideServCancel final
	: public Command
{
public:
	CommandAideServCancel(Module *creator)
		: Command(creator, "aideserv/cancel", 0)
	{
		SetDesc(_("Cancel your ticket if it has not been taken yet"));
		AllowUnregistered(true);
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &) override
	{
		auto *mod = MeAideServ;
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
		source.Reply(_("Ticket \002#%u\002 has been cancelled."), t->id);
		const char *cfmt = Language::Translate(_("[\002%s #%u\002] cancelled by %s"));
		mod->NotifyStaff(source.service, Anope::Format(cfmt, t->queue.c_str(), t->id, u->nick.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Cancels your ticket if no helper has taken it yet."));
		return true;
	}
};

class CommandAideServReport final
	: public Command
{
public:
	CommandAideServReport(Module *creator)
		: Command(creator, "aideserv/report", 2, 2)
	{
		SetDesc(_("File a report against a user"));
		SetSyntax(_("\037nick\037 [\037#channel\037] \037reason\037"));
		AllowUnregistered(true);
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsReportBot(source.service))
		{
			source.Reply(_("Use \002%s\002 to file a report."), MeAideServ->GetReportBot() ? MeAideServ->GetReportBot()->nick.c_str() : "SignalMoi");
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
		MeAideServ->StartReport(source.GetUser(), source.service, target, chan, reason);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_(
			"Files a report against a nickname. Do not discuss the report in a public channel. "
			"The ticket is created as soon as the reason is clear; extra evidence can be sent afterwards as private messages. "
			"The reported user is not notified."
		));
		return true;
	}
};

class CommandAideServList final
	: public Command
{
public:
	CommandAideServList(Module *creator)
		: Command(creator, "aideserv/list", 0, 1)
	{
		SetDesc(_("List tickets"));
		SetSyntax(_("[UNASSIGNED | ASSIGNED | ME | ALL | CLOSED]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}

		Anope::string filter = params.empty() ? "UNASSIGNED" : params[0];
		auto queue = MeAideServ->QueueFor(source.service);
		bool allq = filter.equals_ci("ALL");
		Anope::string me = source.GetAccount() ? source.GetAccount()->display : source.GetNick();

		ListFormatter list(source.GetAccount());
		list.AddColumn(_("ID")).AddColumn(_("Queue")).AddColumn(_("Status")).AddColumn(_("Opener")).AddColumn(_("Helper")).AddColumn(_("Summary"));
		list.SetFlexible(_("{id} ({queue}/{status}) {opener} — {summary}"));

		for (auto *t : Tickets)
		{
			if (!allq && !t->queue.equals_ci(queue))
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
		source.Reply(_("Lists tickets for this bot's queue. Helpers see unassigned tickets by default."));
		return true;
	}
};

class CommandAideServNext final
	: public Command
{
public:
	CommandAideServNext(Module *creator)
		: Command(creator, "aideserv/next", 0)
	{
		SetDesc(_("Take the oldest waiting ticket"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &) override
	{
		if (!MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}
		auto *t = MeAideServ->OldestUnassigned(MeAideServ->QueueFor(source.service));
		if (!t)
		{
			source.Reply(_("There are no waiting tickets."));
			return;
		}
		Anope::string helper = source.GetAccount() ? source.GetAccount()->display : source.GetNick();
		MeAideServ->Assign(source, t, helper);
		source.Reply(_("Opener: \002%s\002 (%s) — %s"), t->opener_nick.c_str(), t->opener_host.c_str(), t->summary.c_str());
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Takes the oldest unassigned ticket in this queue."));
		return true;
	}
};

class CommandAideServPickup final
	: public Command
{
public:
	CommandAideServPickup(Module *creator)
		: Command(creator, "aideserv/pickup", 1, 1)
	{
		SetDesc(_("Take a specific ticket"));
		SetSyntax(_("\037id\037|\037nick\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}

		AideTicket *t = nullptr;
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		if (!key.empty() && key[0] >= '0' && key[0] <= '9')
			t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t)
		{
			auto queue = MeAideServ->QueueFor(source.service);
			for (auto *cand : Tickets)
			{
				if (!cand->status.equals_ci(STATUS_CLOSED) && cand->queue.equals_ci(queue) && cand->opener_nick.equals_ci(params[0]))
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
		MeAideServ->Assign(source, t, helper);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Takes ticket \037id\037 or the open ticket of \037nick\037."));
		return true;
	}
};

class CommandAideServShow final
	: public Command
{
public:
	CommandAideServShow(Module *creator)
		: Command(creator, "aideserv/show", 1, 1)
	{
		SetDesc(_("Show a ticket"));
		SetSyntax(_("\037id\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}
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

class CommandAideServClose final
	: public Command
{
public:
	CommandAideServClose(Module *creator)
		: Command(creator, "aideserv/close", 1, 2)
	{
		SetDesc(_("Close a ticket"));
		SetSyntax(_("\037id\037 [\037reason\037]"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}
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
		User *opener = User::Find(t->opener_nick, true);
		if (opener)
		{
			if (reason.empty())
				opener->SendMessage(source.service, _("Your ticket \002#%u\002 has been closed."), t->id);
			else
				opener->SendMessage(source.service, _("Your ticket \002#%u\002 has been closed: %s"), t->id, reason.c_str());
		}
		const char *clfmt = Language::Translate(_("[\002%s #%u\002] closed by %s (%s)"));
		MeAideServ->NotifyStaff(source.service, Anope::Format(clfmt, t->queue.c_str(), t->id, source.GetNick().c_str(), t->close_reason.c_str()));
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Closes a ticket. The opener is notified."));
		return true;
	}
};

class CommandAideServAddNote final
	: public Command
{
public:
	CommandAideServAddNote(Module *creator)
		: Command(creator, "aideserv/addnote", 2, 2)
	{
		SetDesc(_("Add an internal note to a ticket"));
		SetSyntax(_("\037id\037 \037text\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}
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

class CommandAideServReassign final
	: public Command
{
public:
	CommandAideServReassign(Module *creator)
		: Command(creator, "aideserv/reassign", 2, 2)
	{
		SetDesc(_("Reassign a ticket to another helper"));
		SetSyntax(_("\037id\037 \037helper\037"));
		RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (!MeAideServ->IsManager(source.GetUser()) && !MeAideServ->IsStaff(source.GetUser()))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}
		Anope::string key = params[0];
		if (!key.empty() && key[0] == '#')
			key.erase(key.begin());
		auto *t = AideTicket::FindId(Anope::Convert<unsigned>(key, 0));
		if (!t || t->status.equals_ci(STATUS_CLOSED))
		{
			source.Reply(_("Ticket not found."));
			return;
		}
		MeAideServ->Assign(source, t, params[1]);
	}

	bool OnHelp(CommandSource &source, const Anope::string &) override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Gives the ticket to another helper."));
		return true;
	}
};

class ModuleAideServCommands final
	: public ModuleAideServ
{
	CommandAideServWait cmdwait;
	CommandAideServCancel cmdcancel;
	CommandAideServReport cmdreport;
	CommandAideServList cmdlist;
	CommandAideServNext cmdnext;
	CommandAideServPickup cmdpickup;
	CommandAideServShow cmdshow;
	CommandAideServClose cmdclose;
	CommandAideServAddNote cmdnote;
	CommandAideServReassign cmdreassign;

public:
	ModuleAideServCommands(const Anope::string &modname, const Anope::string &creator)
		: ModuleAideServ(modname, creator)
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
	{
	}
};

MODULE_INIT(ModuleAideServCommands)
