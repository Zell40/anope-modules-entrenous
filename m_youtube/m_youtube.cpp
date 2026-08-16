/*
2024 Jean "reverse" Chevronnet
Module for Anope IRC Services v2.1.
Botserv read and answer youtube links.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/// BEGIN CMAKE
/// find_package(CURL REQUIRED)
/// target_link_libraries(${SO} PRIVATE CURL::libcurl vendored_yyjson)
/// END CMAKE

#include "module.h"
#include <curl/curl.h>
#include "yyjson/yyjson.h"
#include <regex>
#include <chrono>
#include <sstream>
#include <cctype>
#include <algorithm>

// ── Persistent video cache (written to m_youtube.module.json) ──

static constexpr const char *YOUTUBE_CACHE_DATA_TYPE = "YouTubeCache";

class YouTubeCacheEntry;
using youtube_cache_map = Anope::unordered_map<YouTubeCacheEntry *>;
static Serialize::Checker<youtube_cache_map> YouTubeCacheList(YOUTUBE_CACHE_DATA_TYPE);

class YouTubeCacheEntry final : public Serializable
{
public:
    Anope::string video_id;
    Anope::string title;
    Anope::string duration;
    Anope::string view_count;
    time_t expires_at = 0;
    // Usage stats
    uint64_t share_count = 0;
    time_t first_seen = 0;
    time_t last_seen = 0;
    Anope::string last_channel;
    Anope::string last_nick;

    explicit YouTubeCacheEntry(const Anope::string &vid)
        : Serializable(YOUTUBE_CACHE_DATA_TYPE)
        , video_id(vid)
    {
        YouTubeCacheList->insert_or_assign(vid, this);
    }

    ~YouTubeCacheEntry() override
    {
        YouTubeCacheList->erase(this->video_id);
    }

    bool IsExpired() const { return expires_at <= Anope::CurTime; }

    static YouTubeCacheEntry *Find(const Anope::string &vid)
    {
        auto it = YouTubeCacheList->find(vid);
        return it != YouTubeCacheList->end() ? it->second : nullptr;
    }

    static YouTubeCacheEntry *FindOrCreate(const Anope::string &vid)
    {
        auto *e = Find(vid);
        return e ? e : new YouTubeCacheEntry(vid);
    }
};

class YouTubeCacheDataType final : public Serialize::Type
{
public:
    YouTubeCacheDataType()
        : Serialize::Type(YOUTUBE_CACHE_DATA_TYPE)
    {
    }

    void Serialize(Serializable *obj, Serialize::Data &data) const override
    {
        const auto *e = static_cast<const YouTubeCacheEntry *>(obj);
        data.Store("video_id",     e->video_id);
        data.Store("title",        e->title);
        data.Store("duration",     e->duration);
        data.Store("view_count",   e->view_count);
        data.Store("expires_at",   e->expires_at);
        data.Store("share_count",  e->share_count);
        data.Store("first_seen",   e->first_seen);
        data.Store("last_seen",    e->last_seen);
        data.Store("last_channel", e->last_channel);
        data.Store("last_nick",    e->last_nick);
    }

    Serializable *Unserialize(Serializable *obj, Serialize::Data &data) const override
    {
        Anope::string video_id;
        data.TryLoad("video_id", video_id);
        if (video_id.empty())
            return nullptr;

        YouTubeCacheEntry *e = nullptr;
        if (obj)
            e = anope_dynamic_static_cast<YouTubeCacheEntry *>(obj);
        else
        {
            e = YouTubeCacheEntry::Find(video_id);
            if (!e)
                e = new YouTubeCacheEntry(video_id);
        }

        e->video_id = video_id;
        data.TryLoad("title",        e->title);
        data.TryLoad("duration",     e->duration);
        data.TryLoad("view_count",   e->view_count);
        data.TryLoad("expires_at",   e->expires_at);
        data.TryLoad("share_count",  e->share_count);
        data.TryLoad("first_seen",   e->first_seen);
        data.TryLoad("last_seen",    e->last_seen);
        data.TryLoad("last_channel", e->last_channel);
        data.TryLoad("last_nick",    e->last_nick);
        return e;
    }
};

// ── Per-channel statistics ──

static constexpr const char *YOUTUBE_CHAN_DATA_TYPE = "YouTubeChanStats";

class YouTubeChannelStats;
using youtube_chan_map = Anope::unordered_map<YouTubeChannelStats *>;
static Serialize::Checker<youtube_chan_map> YouTubeChanStatsList(YOUTUBE_CHAN_DATA_TYPE);

class YouTubeChannelStats final : public Serializable
{
public:
    Anope::string channel;
    uint64_t link_count = 0;
    uint64_t kick_count = 0;

    explicit YouTubeChannelStats(const Anope::string &chan)
        : Serializable(YOUTUBE_CHAN_DATA_TYPE)
        , channel(chan)
    {
        YouTubeChanStatsList->insert_or_assign(chan, this);
    }

    ~YouTubeChannelStats() override
    {
        YouTubeChanStatsList->erase(this->channel);
    }

    static YouTubeChannelStats *Find(const Anope::string &chan)
    {
        auto it = YouTubeChanStatsList->find(chan);
        return it != YouTubeChanStatsList->end() ? it->second : nullptr;
    }

    static YouTubeChannelStats *FindOrCreate(const Anope::string &chan)
    {
        auto *e = Find(chan);
        return e ? e : new YouTubeChannelStats(chan);
    }
};

class YouTubeChanStatsDataType final : public Serialize::Type
{
public:
    YouTubeChanStatsDataType()
        : Serialize::Type(YOUTUBE_CHAN_DATA_TYPE)
    {
    }

    void Serialize(Serializable *obj, Serialize::Data &data) const override
    {
        const auto *e = static_cast<const YouTubeChannelStats *>(obj);
        data.Store("channel",    e->channel);
        data.Store("link_count", e->link_count);
        data.Store("kick_count", e->kick_count);
    }

    Serializable *Unserialize(Serializable *obj, Serialize::Data &data) const override
    {
        Anope::string channel;
        data.TryLoad("channel", channel);
        if (channel.empty())
            return nullptr;

        YouTubeChannelStats *e = nullptr;
        if (obj)
            e = anope_dynamic_static_cast<YouTubeChannelStats *>(obj);
        else
        {
            e = YouTubeChannelStats::Find(channel);
            if (!e)
                e = new YouTubeChannelStats(channel);
        }

        e->channel = channel;
        data.TryLoad("link_count", e->link_count);
        data.TryLoad("kick_count", e->kick_count);
        return e;
    }
};

// ── Per-user statistics ──

static constexpr const char *YOUTUBE_USER_DATA_TYPE = "YouTubeUserStats";

class YouTubeUserStats;
using youtube_user_map = Anope::unordered_map<YouTubeUserStats *>;
static Serialize::Checker<youtube_user_map> YouTubeUserStatsList(YOUTUBE_USER_DATA_TYPE);

class YouTubeUserStats final : public Serializable
{
public:
    Anope::string nick;
    uint64_t link_count = 0;
    uint64_t kick_count = 0;

    explicit YouTubeUserStats(const Anope::string &n)
        : Serializable(YOUTUBE_USER_DATA_TYPE)
        , nick(n)
    {
        YouTubeUserStatsList->insert_or_assign(n, this);
    }

    ~YouTubeUserStats() override
    {
        YouTubeUserStatsList->erase(this->nick);
    }

    static YouTubeUserStats *Find(const Anope::string &n)
    {
        auto it = YouTubeUserStatsList->find(n);
        return it != YouTubeUserStatsList->end() ? it->second : nullptr;
    }

    static YouTubeUserStats *FindOrCreate(const Anope::string &n)
    {
        auto *e = Find(n);
        return e ? e : new YouTubeUserStats(n);
    }
};

class YouTubeUserStatsDataType final : public Serialize::Type
{
public:
    YouTubeUserStatsDataType()
        : Serialize::Type(YOUTUBE_USER_DATA_TYPE)
    {
    }

    void Serialize(Serializable *obj, Serialize::Data &data) const override
    {
        const auto *e = static_cast<const YouTubeUserStats *>(obj);
        data.Store("nick",       e->nick);
        data.Store("link_count", e->link_count);
        data.Store("kick_count", e->kick_count);
    }

    Serializable *Unserialize(Serializable *obj, Serialize::Data &data) const override
    {
        Anope::string nick;
        data.TryLoad("nick", nick);
        if (nick.empty())
            return nullptr;

        YouTubeUserStats *e = nullptr;
        if (obj)
            e = anope_dynamic_static_cast<YouTubeUserStats *>(obj);
        else
        {
            e = YouTubeUserStats::Find(nick);
            if (!e)
                e = new YouTubeUserStats(nick);
        }

        e->nick = nick;
        data.TryLoad("link_count", e->link_count);
        data.TryLoad("kick_count", e->kick_count);
        return e;
    }
};

class CommandBSYouTube final : public Command
{
private:
    Anope::string api_key;
    Anope::string prefix;
    Anope::string duration_text;
    Anope::string seen_text;
    Anope::string times_text;
    Anope::string response_format;

    // Spam tracking: key = "channel\nnick" -> list of link timestamps
    std::map<Anope::string, std::vector<time_t>> spam_map;
    // Spam config
    int spam_limit = 3;
    int spam_window = 60;
    bool spam_kick_enabled = true;
    Anope::string spam_kick_reason = "YouTube link spam";
    int cache_ttl = 300;

    static Anope::string UnescapeConfigString(const Anope::string &in)
    {
        // Anope's config parser only treats \" specially. We optionally support common
        // escape sequences here so users can configure IRC formatting codes.
        Anope::string out;

        for (size_t i = 0; i < in.length(); ++i)
        {
            const char ch = in[i];
            if (ch != '\\' || i + 1 >= in.length())
            {
                out.push_back(ch);
                continue;
            }

            const char next = in[i + 1];
            if (next == 'n')
            {
                out.push_back('\n');
                ++i;
            }
            else if (next == 'r')
            {
                out.push_back('\r');
                ++i;
            }
            else if (next == 't')
            {
                out.push_back('\t');
                ++i;
            }
            else if (next == '\\' || next == '"')
            {
                out.push_back(next);
                ++i;
            }
            else if (next == 'x' && i + 3 < in.length() && std::isxdigit(static_cast<unsigned char>(in[i + 2])) && std::isxdigit(static_cast<unsigned char>(in[i + 3])))
            {
                auto hexval = [](char c) -> int
                {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (c >= 'a' && c <= 'f')
                        return 10 + (c - 'a');
                    return 0;
                };

                const int value = (hexval(in[i + 2]) << 4) | hexval(in[i + 3]);
                out.push_back(static_cast<char>(value));
                i += 3;
            }
            else if (next >= '0' && next <= '7')
            {
                // Octal \NNN (up to 3 digits)
                int value = 0;
                size_t j = i + 1;
                for (int digits = 0; digits < 3 && j < in.length(); ++digits, ++j)
                {
                    char oc = in[j];
                    if (oc < '0' || oc > '7')
                        break;
                    value = (value << 3) | (oc - '0');
                }
                out.push_back(static_cast<char>(value & 0xFF));
                i = j - 1;
            }
            else
            {
                // Unknown escape; keep the next char and drop the backslash.
                out.push_back(next);
                ++i;
            }
        }

        return out;
    }

    Anope::string BuildResponse(const Anope::string &title, const Anope::string &duration, const Anope::string &viewCount) const
    {
        if (!this->response_format.empty())
        {
            // Simple token replacement (no printf-style formatting).
            Anope::string out = this->response_format;
            out = out.replace_all_cs("{title}", title);
            out = out.replace_all_cs("{duration}", duration);
            out = out.replace_all_cs("{views}", viewCount);
            out = out.replace_all_cs("{viewCount}", viewCount);
            return out;
        }

        // Backwards-compatible default.
        return this->prefix + " " + title + " - " + this->duration_text + duration + " - " + this->seen_text + viewCount + this->times_text;
    }

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    std::string ParseISO8601Duration(const std::string &duration)
    {
        std::chrono::seconds seconds(0);
        std::smatch match;
        std::regex duration_regex("PT((\\d+)H)?((\\d+)M)?((\\d+)S)?");

        if (std::regex_match(duration, match, duration_regex))
        {
            if (match[2].matched)
                seconds += std::chrono::hours(std::stoi(match[2].str()));
            if (match[4].matched)
                seconds += std::chrono::minutes(std::stoi(match[4].str()));
            if (match[6].matched)
                seconds += std::chrono::seconds(std::stoi(match[6].str()));
        }

        auto h = std::chrono::duration_cast<std::chrono::hours>(seconds).count();
        auto m = std::chrono::duration_cast<std::chrono::minutes>(seconds).count() % 60;
        auto s = seconds.count() % 60;

        std::ostringstream oss;
        if (h > 0) oss << h << "h";
        if (m > 0) oss << m << "m";
        if (s > 0) oss << s << "s";

        return oss.str();
    }

    void FetchYouTubeDetails(const Anope::string &api_key, const Anope::string &video_id, Anope::string &title, Anope::string &duration, Anope::string &viewCount)
    {
        if (api_key.empty())
        {
            Log(LOG_DEBUG) << "YouTube API key is not configured.";
            return;
        }

        // Check persistent cache
        auto *cached = YouTubeCacheEntry::Find(video_id);
        if (cached && !cached->IsExpired())
        {
            title     = cached->title;
            duration  = cached->duration;
            viewCount = cached->view_count;
            Log(LOG_DEBUG) << "m_youtube: cache hit for " << video_id;
            return;
        }

        // Keep payload small.
        const Anope::string api_url = "https://www.googleapis.com/youtube/v3/videos?id=" + video_id
            + "&part=snippet,contentDetails,statistics"
            + "&fields=items(snippet(title),contentDetails(duration),statistics(viewCount))"
            + "&key=" + api_key;

        CURL *curl = curl_easy_init();
        if (curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Anope-m_youtube/1.0");

            std::string response_string;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK)
            {
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                if (http_code != 200)
                {
                    Log(LOG_DEBUG) << "YouTube API HTTP status " << http_code << " for video ID: " << video_id;
                    curl_easy_cleanup(curl);
                    return;
                }

                yyjson_doc *jsonDoc = yyjson_read(response_string.c_str(), response_string.size(), 0);
                if (!jsonDoc)
                {
                    Log() << "JSON Parse Error: failed to parse YouTube API response";
                }
                else
                {
                    yyjson_val *root = yyjson_doc_get_root(jsonDoc);
                    yyjson_val *items = yyjson_obj_get(root, "items");
                    if (items && yyjson_is_arr(items) && yyjson_arr_size(items) > 0)
                    {
                        yyjson_val *item = yyjson_arr_get_first(items);
                        yyjson_val *snippet       = yyjson_obj_get(item, "snippet");
                        yyjson_val *contentDetails = yyjson_obj_get(item, "contentDetails");
                        yyjson_val *statistics    = yyjson_obj_get(item, "statistics");

                        if (snippet && contentDetails && statistics
                            && yyjson_is_obj(snippet) && yyjson_is_obj(contentDetails) && yyjson_is_obj(statistics))
                        {
                            yyjson_val *titleVal    = yyjson_obj_get(snippet, "title");
                            yyjson_val *durationVal = yyjson_obj_get(contentDetails, "duration");
                            yyjson_val *viewVal     = yyjson_obj_get(statistics, "viewCount");

                            if (titleVal && yyjson_is_str(titleVal)
                                && durationVal && yyjson_is_str(durationVal)
                                && viewVal)
                            {
                                title    = yyjson_get_str(titleVal);
                                duration = ParseISO8601Duration(yyjson_get_str(durationVal));

                                if (yyjson_is_str(viewVal))
                                    viewCount = yyjson_get_str(viewVal);
                                else if (yyjson_is_uint(viewVal))
                                    viewCount = Anope::ToString(yyjson_get_uint(viewVal));
                                else if (yyjson_is_sint(viewVal))
                                    viewCount = Anope::ToString(yyjson_get_sint(viewVal));
                                else
                                    Log() << "Invalid JSON response: statistics.viewCount is not a string or integer";

                                // Store in persistent cache
                                if (!title.empty())
                                {
                                    auto *entry = YouTubeCacheEntry::FindOrCreate(video_id);
                                    entry->title      = title;
                                    entry->duration   = duration;
                                    entry->view_count = viewCount;
                                    entry->expires_at = Anope::CurTime + cache_ttl;
                                }
                            }
                            else
                            {
                                Log() << "Invalid JSON response: missing/invalid members in snippet, contentDetails, or statistics";
                            }
                        }
                        else
                        {
                            Log() << "Invalid JSON response: missing/invalid snippet, contentDetails, or statistics";
                        }
                    }
                    else
                    {
                        Log() << "Invalid JSON response: 'items' field missing, not an array, or empty";
                    }
                    yyjson_doc_free(jsonDoc);
                }
            }
            else
            {
                Log() << "CURL Error: " << curl_easy_strerror(res);
            }

            curl_easy_cleanup(curl);
        }
        else
        {
            Log() << "CURL Initialization Error";
        }
    }

    void HandleYouTubeLink(const Anope::string &api_key, ChannelInfo *ci, Channel *c, User *u, const Anope::string &message)
    {
        try
        {
            // Supports:
            // - https://www.youtube.com/watch?v=VIDEOID
            // - https://youtu.be/VIDEOID
            // - https://www.youtube.com/shorts/VIDEOID
            // - https://www.youtube.com/embed/VIDEOID
            // Also accepts m.youtube.com and trailing query/fragment.
            std::regex youtube_regex(
                "https?://(?:www\\.|m\\.)?(?:youtube\\.com/(?:watch\\?v=|shorts/|embed/)|youtu\\.be/)([A-Za-z0-9_-]{11})(?:[?&#/].*)?",
                std::regex::icase);
            std::smatch match;
            if (std::regex_search(message.begin(), message.end(), match, youtube_regex) && match.size() > 1)
            {
                // Spam / rate-limit check (only for live channel messages)
                if (u && c && spam_kick_enabled && ci->bi)
                {
                    Anope::string spam_key = ci->name + "\n" + u->nick;
                    auto &timestamps = spam_map[spam_key];
                    const time_t now = Anope::CurTime;

                    // Drop timestamps outside the window
                    timestamps.erase(
                        std::remove_if(timestamps.begin(), timestamps.end(),
                            [&](time_t t) { return now - t > spam_window; }),
                        timestamps.end());

                    timestamps.push_back(now);

                    if (static_cast<int>(timestamps.size()) > spam_limit)
                    {
                        timestamps.clear();
                        YouTubeChannelStats::FindOrCreate(ci->name)->kick_count++;
                        YouTubeUserStats::FindOrCreate(u->nick)->kick_count++;
                        Log(LOG_DEBUG) << "m_youtube: kicking " << u->nick << " from " << ci->name << " for URL spam";
                        c->Kick(ci->bi, u, spam_kick_reason);
                        return;
                    }
                }

                Anope::string video_id = match.str(1).c_str();
                Anope::string title, duration, viewCount;
                FetchYouTubeDetails(api_key, video_id, title, duration, viewCount);

                if (!title.empty() && !duration.empty() && !viewCount.empty())
                {
                    IRCD->SendPrivmsg(*ci->bi, ci->name, this->BuildResponse(title, duration, viewCount));
                    ci->bi->lastmsg = Anope::CurTime;

                    if (u && c)
                    {
                        YouTubeChannelStats::FindOrCreate(ci->name)->link_count++;
                        YouTubeUserStats::FindOrCreate(u->nick)->link_count++;
                        auto *ce = YouTubeCacheEntry::Find(video_id);
                        if (ce)
                        {
                            ce->share_count++;
                            ce->last_seen    = Anope::CurTime;
                            ce->last_channel = ci->name;
                            ce->last_nick    = u->nick;
                            if (ce->first_seen == 0)
                                ce->first_seen = Anope::CurTime;
                        }
                    }
                }
                else
                {
                    Log(LOG_DEBUG) << "Failed to fetch YouTube details for video ID: " << video_id;
                }
            }
        }
        catch (const std::regex_error &e)
        {
            Log() << "Regex error: " << e.what();
        }
    }

public:
    CommandBSYouTube(Module *creator) : Command(creator, "botserv/youtube", 2, 2)
    {
        this->SetDesc(_("Handles YouTube links and fetches video details"));
        this->SetSyntax(_("\037channel\037 \037message\037"));

        // Reasonable defaults (can be overridden in config).
        this->prefix = "\x02\x03""01,00You\x03""00,04Tube\x0F\x02";
        this->duration_text = "Duration: ";
        this->seen_text = "Seen: ";
        this->times_text = " times.";
    }

    void SetAPIKey(const Anope::string &key)
    {
        this->api_key = key;
    }

    void SetResponseConfig(const Anope::string &prefix,
                           const Anope::string &duration_text,
                           const Anope::string &seen_text,
                           const Anope::string &times_text,
                           const Anope::string &response_format)
    {
        if (!prefix.empty())
            this->prefix = UnescapeConfigString(prefix);
        if (!duration_text.empty())
            this->duration_text = UnescapeConfigString(duration_text);
        if (!seen_text.empty())
            this->seen_text = UnescapeConfigString(seen_text);
        if (!times_text.empty())
            this->times_text = UnescapeConfigString(times_text);
        this->response_format = UnescapeConfigString(response_format);
    }

    void SetSpamConfig(int limit, int window, bool kick_enabled, const Anope::string &kick_reason, int ttl)
    {
        this->spam_limit       = limit;
        this->spam_window      = window;
        this->spam_kick_enabled = kick_enabled;
        this->spam_kick_reason = kick_reason.empty() ? "YouTube link spam" : kick_reason;
        this->cache_ttl        = ttl;
    }

    void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
    {
        const Anope::string &channel = params[0];
        const Anope::string &message = params[1];

        ChannelInfo *ci = ChannelInfo::Find(channel);
        if (ci == NULL)
        {
            source.Reply(CHAN_X_NOT_REGISTERED, channel.c_str());
            return;
        }

        if (!source.AccessFor(ci).HasPriv("SAY") && !source.HasPriv("botserv/administration"))
        {
            source.Reply(ACCESS_DENIED);
            return;
        }

        if (!ci->bi)
        {
            source.Reply(BOT_NOT_ASSIGNED);
            return;
        }

        if (!ci->c || !ci->c->FindUser(ci->bi))
        {
            source.Reply(BOT_NOT_ON_CHANNEL, ci->name.c_str());
            return;
        }

        if (!message.empty() && message[0] == '\001')
        {
            this->OnSyntaxError(source, "");
            return;
        }

        HandleYouTubeLink(this->api_key, ci, nullptr, nullptr, message);

        bool override = !source.AccessFor(ci).HasPriv("SAY");
        Log(override ? LOG_OVERRIDE : LOG_COMMAND, source, this, ci) << "to fetch YouTube info";
    }

    bool OnHelp(CommandSource &source, const Anope::string &subcommand) override
    {
        this->SendSyntax(source);
        source.Reply(" ");
        source.Reply(_("Handles YouTube links and fetches video details when a YouTube link is posted in the channel."));
        return true;
    }

    friend class YouTubeModule;
};

class CommandBSYouTubeStats final : public Command
{
private:
    static Anope::string FormatTime(time_t t)
    {
        if (t == 0)
            return "never";
        char buf[32];
        struct tm *tm_info = localtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
        return buf;
    }

public:
    CommandBSYouTubeStats(Module *creator) : Command(creator, "botserv/ytstats", 2, 2)
    {
        this->SetDesc(_("Show YouTube module usage statistics"));
        this->SetSyntax(_("VIDEO \037video_id\037"));
        this->SetSyntax(_("CHANNEL \037#channel\037"));
        this->SetSyntax(_("USER \037nick\037"));
    }

    // Send a message either through the channel bot (fantasy) or via source.Reply (PM).
    void Say(CommandSource &source, const char *fmt, ...) ATTR_FORMAT(3, 4)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        if (source.c && source.c->ci && source.c->ci->bi)
        {
            IRCD->SendPrivmsg(*source.c->ci->bi, source.c->name, buf);
            source.c->ci->bi->lastmsg = Anope::CurTime;
        }
        else
        {
            source.Reply("%s", buf);
        }
    }

    void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
    {
        const Anope::string type    = params[0].upper();
        const Anope::string &target = params[1];

        if (type == "VIDEO")
        {
            const YouTubeCacheEntry *e = YouTubeCacheEntry::Find(target);
            if (!e)
            {
                Say(source, "No data found for video \002%s\002.", target.c_str());
                return;
            }
            Say(source, "Video \002%s\002: \"%s\"  |  %s  |  %s views",
                target.c_str(), e->title.c_str(),
                e->duration.c_str(), e->view_count.c_str());
            Say(source, "  Shared \002%s\002 time(s)  |  First seen: %s  |  Last seen: %s",
                Anope::ToString(e->share_count).c_str(),
                FormatTime(e->first_seen).c_str(),
                FormatTime(e->last_seen).c_str());
            if (!e->last_nick.empty())
                Say(source, "  Last posted by \002%s\002 in \002%s\002",
                    e->last_nick.c_str(), e->last_channel.c_str());
        }
        else if (type == "CHANNEL")
        {
            const YouTubeChannelStats *e = YouTubeChannelStats::Find(target);
            if (!e)
            {
                Say(source, "No data found for channel \002%s\002.", target.c_str());
                return;
            }
            Say(source, "Channel \002%s\002: \002%s\002 link(s) posted, \002%s\002 spam kick(s).",
                target.c_str(),
                Anope::ToString(e->link_count).c_str(),
                Anope::ToString(e->kick_count).c_str());
        }
        else if (type == "USER")
        {
            const YouTubeUserStats *e = YouTubeUserStats::Find(target);
            if (!e)
            {
                Say(source, "No data found for user \002%s\002.", target.c_str());
                return;
            }
            Say(source, "User \002%s\002: \002%s\002 link(s) posted, \002%s\002 spam kick(s).",
                target.c_str(),
                Anope::ToString(e->link_count).c_str(),
                Anope::ToString(e->kick_count).c_str());
        }
        else
        {
            this->OnSyntaxError(source, type);
        }
    }

    bool OnHelp(CommandSource &source, const Anope::string &) override
    {
        this->SendSyntax(source);
        source.Reply(" ");
        source.Reply(_("Shows YouTube module usage statistics."));
        source.Reply(_("VIDEO <id>       Show share count, duration, and first/last seen info."));
        source.Reply(_("CHANNEL <#chan>  Show total links posted and spam kicks for a channel."));
        source.Reply(_("USER <nick>      Show total links posted and spam kicks for a user."));
        return true;
    }
};

class YouTubeModule final : public Module
{
    CommandBSYouTube commandbsYouTube;
    CommandBSYouTubeStats commandbsYouTubeStats;
    YouTubeCacheDataType youtube_cache_type;
    YouTubeChanStatsDataType youtube_chan_stats_type;
    YouTubeUserStatsDataType youtube_user_stats_type;
    Anope::string api_key;
    Anope::string prefix;
    Anope::string duration_text;
    Anope::string seen_text;
    Anope::string times_text;
    Anope::string response_format;

    void LoadConfig(Configuration::Conf &conf)
    {
        const auto &modconf = conf.GetModule(this);

        // Prefer youtube_api_key but accept api_key for convenience.
        this->api_key = modconf.Get<Anope::string>("youtube_api_key");
        if (this->api_key.empty())
            this->api_key = modconf.Get<Anope::string>("api_key");

        if (this->api_key.empty())
            Log() << "m_youtube: youtube_api_key not set in modules.conf; video info lookups are disabled.";

        // Response text customization.
        this->prefix = modconf.Get<Anope::string>("prefix", "\x02\x03""01,00You\x03""00,04Tube\x0F\x02");

        // Allow short key names for convenience.
        this->duration_text = modconf.Get<Anope::string>("duration_text");
        if (this->duration_text.empty())
            this->duration_text = modconf.Get<Anope::string>("duration", "Duration: ");

        this->seen_text = modconf.Get<Anope::string>("seen_text");
        if (this->seen_text.empty())
            this->seen_text = modconf.Get<Anope::string>("seen", "Seen: ");

        this->times_text = modconf.Get<Anope::string>("times_text");
        if (this->times_text.empty())
            this->times_text = modconf.Get<Anope::string>("times", " times.");

        this->response_format = modconf.Get<Anope::string>("response_format");
        if (this->response_format.empty())
            this->response_format = modconf.Get<Anope::string>("format");

        commandbsYouTube.SetAPIKey(this->api_key);
        commandbsYouTube.SetResponseConfig(this->prefix, this->duration_text, this->seen_text, this->times_text, this->response_format);

        const int spam_limit        = modconf.Get<int>("spam_limit", "3");
        const int spam_window       = modconf.Get<int>("spam_window", "60");
        const bool spam_kick        = modconf.Get<bool>("spam_kick", "yes");
        const Anope::string spam_reason = modconf.Get<Anope::string>("spam_kick_reason", "YouTube link spam");
        const int cache_ttl         = modconf.Get<int>("cache_ttl", "300");
        commandbsYouTube.SetSpamConfig(spam_limit, spam_window, spam_kick, spam_reason, cache_ttl);
    }

public:
    YouTubeModule(const Anope::string &modname, const Anope::string &creator)
        : Module(modname, creator, THIRD)
        , commandbsYouTube(this)
        , commandbsYouTubeStats(this)
    {
        // Configuration is loaded via OnReload.
    }

    ~YouTubeModule()
    {
        // Delete every in-memory record on unload. Each ~Serializable removes
        // itself from its list (and from Anope's global SerializableItems), so
        // this must run while the module's code is still mapped -- otherwise the
        // objects leak and leave dangling vtable pointers in the global list.
        while (!YouTubeCacheList->empty())
        {
            auto it = YouTubeCacheList->begin();
            YouTubeCacheEntry *e = it->second;
            if (!e) { YouTubeCacheList->erase(it); continue; }
            delete e;
        }
        while (!YouTubeChanStatsList->empty())
        {
            auto it = YouTubeChanStatsList->begin();
            YouTubeChannelStats *e = it->second;
            if (!e) { YouTubeChanStatsList->erase(it); continue; }
            delete e;
        }
        while (!YouTubeUserStatsList->empty())
        {
            auto it = YouTubeUserStatsList->begin();
            YouTubeUserStats *e = it->second;
            if (!e) { YouTubeUserStatsList->erase(it); continue; }
            delete e;
        }
    }

    void OnReload(Configuration::Conf &conf) override
    {
        this->LoadConfig(conf);
    }

    void OnShutdown() override
    {
        Anope::SaveDatabases();
    }

    void OnRestart() override
    {
        Anope::SaveDatabases();
    }

    void OnPrivmsg(User *u, Channel *c, Anope::string &msg, const Anope::map<Anope::string> &tags) override
    {
        if (u == nullptr || c == nullptr || c->ci == nullptr || c->ci->bi == nullptr || msg.empty() || msg[0] == '\1')
            return;

        commandbsYouTube.HandleYouTubeLink(this->api_key, c->ci, c, u, msg);
    }
};

MODULE_INIT(YouTubeModule)
