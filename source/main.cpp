#include <switch.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <time.h>

// Number of entries to process at one time
#define MAX_PROCESS_ENTRIES 4096

// Permits printing of u128 values
std::ostream&
operator<<(std::ostream& dest, __uint128_t value) {
    std::ostream::sentry s( dest );
    if ( s ) {
        __uint128_t tmp = value < 0 ? -value : value;
        char buffer[ 128 ];
        char* d = std::end( buffer );
        do
        {
            -- d;
            *d = "0123456789"[ tmp % 10 ];
            tmp /= 10;
        } while ( tmp != 0 );
        if ( value < 0 ) {
            -- d;
            *d = '-';
        }
        int len = std::end( buffer ) - d;
        if ( dest.rdbuf()->sputn( d, len ) != len ) {
            dest.setstate( std::ios_base::badbit );
        }
    }
    return dest;
}

// Just so function decs. are easier to read
typedef u64 TitleID;

// Enumeration for event type in PlayEvent struct
enum PlayEventType {
    PlayEvent_Applet,       // PlayEvent contains applet event
    PlayEvent_Account       // PlayEvent contains account event
};

// Enumeration for applet/account event type in PlayEvent struct
enum EventType {
    Applet_Launch,          // Applet launched
    Applet_Exit,            // Applet quit
    Applet_InFocus,         // Applet gained focus
    Applet_OutFocus,        // Applet lost focus
    Account_Active,         // Account selected
    Account_Inactive        // Account closed?
};

// PlayEvents are parsed PdmPlayEvents containing only necessary information
struct PlayEvent {
    PlayEventType type;     // Type of PlayEvent
    AccountUid userID;      // UserID
    TitleID titleID;        // TitleID
    EventType eventType;    // See EventType enum
    u64 clockTimestamp;     // Time of event
    u64 steadyTimestamp;    // Steady timestamp (used for calculating duration)
};

// PdmPlayStatistics but only the necessary things
struct PlayStatistics {
    TitleID titleID;        // TitleID of these stats
    u64 firstPlayed;        // Timestamp of first launch
    u64 lastPlayed;         // Timestamp of last play (exit)
    u64 playtime;           // Total playtime in seconds
    u32 launches;           // Total launches
};

// RecentPlayStatistics struct is similar to PdmPlayStatistics but
// only contains recent values
struct RecentPlayStatistics {
    TitleID titleID;        // TitleID of these statistics
    u64 playtime;           // Total playtime in seconds
    u32 launches;           // Total launches
};

// Comparison of AccountUids
bool operator == (const AccountUid &a, const AccountUid &b) {
    if (a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1]) {
        return true;
    }
    return false;
}

// Struct used for analyzing/splitting up play sessions
struct PD_Session {
    size_t index;   // Index of first (launch) event
    size_t num;     // Number of events for this session
};

std::vector<PlayEvent *> g_events;

std::vector<PD_Session> getPDSessions(TitleID titleID, AccountUid userID, u64 start_ts, u64 end_ts) {
    // Break each "session" apart and keep if matching titleID and userID
    std::vector<PD_Session> sessions;
    size_t a = 0;
    while (a < g_events.size()) {
        if (g_events[a]->eventType == Applet_Launch) {
            // Find end of session (or start of next session if switch crashed before exit)
            size_t s = a;
            bool time_c = false;
            bool titleID_c = false;
            if (titleID == 0 || g_events[a]->titleID == titleID) {
                titleID_c = true;
            }
            bool userID_c = false;

            a++;
            bool end = false;
            while (a < g_events.size() && !end) {
                // Check if every event is in the required range
                if (g_events[a]->clockTimestamp >= start_ts && g_events[a]->clockTimestamp <= end_ts) {
                    time_c = true;
                }
                // Also check if there is an event prior to event and after
                if (a > s) {
                    if (g_events[a]->clockTimestamp > end_ts && g_events[a-1]->clockTimestamp < start_ts) {
                        time_c = true;
                    }
                }

                switch (g_events[a]->eventType) {
                    // Check userID whenever account event encountered
                    case Account_Active:
                    case Account_Inactive:
                        if (g_events[a]->userID == userID) {
                            userID_c = true;
                        }
                        break;

                    // Exit indicates end of session
                    case Applet_Exit:
                        if (time_c && titleID_c && userID_c) {
                            struct PD_Session st;
                            st.index = s;
                            st.num = a - s + 1;
                            sessions.push_back(st);
                        }
                        end = true;
                        break;

                    // Encountering another launch also indicates end of session (due to crash)
                    case Applet_Launch:
                        if (time_c && titleID_c && userID_c) {
                            struct PD_Session st;
                            st.index = s;
                            st.num = a - s;
                            sessions.push_back(st);
                        }
                        end = true;
                        a--;
                        break;

                    default:
                        break;
                }
                a++;
            }
        } else {
            a++;
        }
    }

    return sessions;
}

RecentPlayStatistics * countPlaytime(std::vector<PD_Session> sessions, u64 start_ts, u64 end_ts) {
    // Count playtime + launches
    RecentPlayStatistics * stats = new RecentPlayStatistics;
    stats->titleID = 0;
    stats->playtime = 0;
    stats->launches = 0;
    if (sessions.size() == 0) {
        return stats;
    }
    // Iterate over valid sessions to calculate statistics
    for (size_t i = 0; i < sessions.size(); i++) {
        stats->launches++;
        u64 last_ts = 0;
        u64 last_clock = 0;
        bool in_before = false;
        bool done = false;
        for (size_t j = sessions[i].index; j < sessions[i].index + sessions[i].num; j++) {
            if (done) {
                break;
            }
            switch (g_events[j]->eventType) {
                case Applet_Launch:
                    // Skip to first in focus event
                    while (j+1 < g_events.size()) {
                        if (g_events[j+1]->eventType != Applet_InFocus) {
                            j++;
                        } else {
                            break;
                        }
                    }
                case Applet_Exit:
                case Account_Active:
                case Account_Inactive:
                    break;
                case Applet_InFocus:
                    last_ts = g_events[j]->steadyTimestamp;
                    last_clock = g_events[j]->clockTimestamp;
                    in_before = false;
                    if (g_events[j]->clockTimestamp < start_ts) {
                        last_clock = start_ts;
                        in_before = true;
                    } else if (g_events[j]->clockTimestamp >= end_ts) {
                        done = true;
                    }
                    break;
                case Applet_OutFocus:
                    if (g_events[j]->clockTimestamp >= end_ts) {
                        stats->playtime += (end_ts - last_clock);
                    } else if (g_events[j]->clockTimestamp >= start_ts) {
                        if (in_before) {
                            stats->playtime += (g_events[j]->clockTimestamp - last_clock);
                        } else {
                            stats->playtime += (g_events[j]->steadyTimestamp - last_ts);
                        }
                    }
                    // Move to last out focus (I don't know why the log has multiple)
                    while (j+1 < g_events.size()) {
                        if (g_events[j+1]->eventType == Applet_OutFocus) {
                            j++;
                        } else {
                            break;
                        }
                    }
                    break;
            }
        }
    }
    return stats;
}

RecentPlayStatistics * getRecentStatisticsForUser(u64 start_ts, u64 end_ts, AccountUid userID) {
    return countPlaytime(getPDSessions(0, userID, start_ts, end_ts), start_ts, end_ts);
}

RecentPlayStatistics * getRecentStatisticsForTitleAndUser(TitleID titleID, u64 start_ts, u64 end_ts, AccountUid userID) {
    RecentPlayStatistics * s = countPlaytime(getPDSessions(titleID, userID, start_ts, end_ts), start_ts, end_ts);
    s->titleID = titleID;
    return s;
}

PlayStatistics * getStatisticsForUser(TitleID titleID, AccountUid userID) {
    PdmPlayStatistics tmp;
    pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(titleID, userID, false, &tmp);
    PlayStatistics * stats = new PlayStatistics;
    stats->firstPlayed = tmp.first_timestamp_user;
    stats->lastPlayed = tmp.last_timestamp_user;
    stats->playtime = tmp.playtime / 1000 / 1000 / 1000; //the unit of playtime in PdmPlayStatistics is ns
    stats->launches = tmp.total_launches;
    return stats;
}

int main(void){
    consoleInit(NULL);
    pdmqryInitialize();

    // File to write to
    std::ofstream out("playlog.txt");
    std::cout << "Beginning dump/parsing..." << std::endl;
    consoleUpdate(NULL);

    // Position of first event to read
    u32 offset = 0;
    // Total pEvents read in iteration
    s32 total_read = 1;

    s32 valid = 0;

    // Array to store read events
    PdmPlayEvent * pEvents = new PdmPlayEvent[MAX_PROCESS_ENTRIES];

    // Read all events
    while (total_read > 0) {
        Result rc = pdmqryQueryPlayEvent(offset, pEvents, MAX_PROCESS_ENTRIES, &total_read);
        if (R_SUCCEEDED(rc)) {
            // Set next read position to next event
            offset += total_read;

            // Process read events
            for (s32 i = 0; i < total_read; i++) {
                PlayEvent * event;

                // Populate PlayEvent based on event type
                switch (pEvents[i].play_event_type) {
                    case PdmPlayEventType_Account: {
                        // Ignore this event if type is 2
                        if (pEvents[i].event_data.account.type == 2) {
                            continue;
                        }
                        event = new PlayEvent;
                        event->type = PlayEvent_Account;

                        // UserID words are wrong way around (why Nintendo?)
                        event->userID.uid[0] = pEvents[i].event_data.account.uid[0];
                        event->userID.uid[0] = (event->userID.uid[0] << 32) | pEvents[i].event_data.account.uid[1];
                        event->userID.uid[1] = (event->userID.uid[1] << 32) | pEvents[i].event_data.account.uid[2];
                        event->userID.uid[1] = (event->userID.uid[1] << 32) | pEvents[i].event_data.account.uid[3];

                        out << "ACCOUNT: ";
                        // Print userID
                        out << std::hex << event->userID.uid[1] << std::dec << "_";
                        out << std::hex << event->userID.uid[0] << std::dec << " ";

                        // Set account event type
                        switch (pEvents[i].event_data.account.type) {
                            case 0:
                                event->eventType = Account_Active;
                                out << "Login";
                                break;
                            case 1:
                                event->eventType = Account_Inactive;
                                out << "Logout";
                                break;
                        }

                        break;
                    }
                    case PdmPlayEventType_Applet: {
                        bool hasUnk = (pEvents[i].event_data.applet.log_policy == PdmPlayLogPolicy_Unknown3);

                        // Ignore this event based on log policy
                        if (pEvents[i].event_data.applet.log_policy != PdmPlayLogPolicy_All) {
                            continue;
                        }
                        event = new PlayEvent;
                        event->type = PlayEvent_Applet;

                        // Join two halves of title ID
                        event->titleID = pEvents[i].event_data.applet.program_id[0];
                        event->titleID = (event->titleID << 32) | pEvents[i].event_data.applet.program_id[1];

                        if (hasUnk) {
                            out << "APPLET (unk): ";
                        } else {
                            out << "APPLET: ";
                        }

                        // Print title ID
                        out << std::hex << event->titleID << std::dec << " ";

                        // Set applet event type
                        switch (pEvents[i].event_data.applet.event_type) {
                            case PdmAppletEventType_Launch:
                                event->eventType = Applet_Launch;
                                out << "Launch";
                                break;
                            case PdmAppletEventType_Exit:
                            case PdmAppletEventType_Exit5:
                            case PdmAppletEventType_Exit6:
                                event->eventType = Applet_Exit;
                                out << "Exit";
                                break;
                            case PdmAppletEventType_InFocus:
                                event->eventType = Applet_InFocus;
                                out << "In Focus";
                                break;
                            case PdmAppletEventType_OutOfFocus:
                            case PdmAppletEventType_OutOfFocus4:
                                event->eventType = Applet_OutFocus;
                                out << "Out Focus";
                                break;
                        }
                        break;
                    }
                    // Do nothing for other event types
                    default:
                        continue;
                        break;
                }

                // Set timestamps
                event->clockTimestamp = pEvents[i].timestamp_user;
                event->steadyTimestamp = pEvents[i].timestamp_steady;

                // Add PlayEvent to vector
                g_events.push_back(event);

                // Print timestamps
                time_t timestamp = event->clockTimestamp;
                struct tm * t = localtime(&timestamp);
                char buf[100];
                strftime(buf, 100, "%d %B %Y %I:%M %p", t);
                out << " " << buf << " ";

                timestamp = event->steadyTimestamp;
                out << timestamp << std::endl;

                valid++;
            }
        }
    }

    // Free memory allocated to array
    delete[] pEvents;

#if 0
    TitleID titleID = 0x1008cf01baac000;
    AccountUid userID;
    userID.uid[1] = 0xfdf44e2c3b8b11b6;
    userID.uid[0] = 0x100000003bcf8802;
    char buf[100];
    PlayStatistics *ps = getStatisticsForUser(titleID, userID);
    out << "User ID: " << std::hex << userID.uid[1] << std::dec << "_" << std::hex << userID.uid[0] << std::dec << " ";
    out << ", TitleID: " << std::hex << titleID << std::dec << std::endl;
    time_t timestamp = ps->firstPlayed;
    struct tm *t = localtime(&timestamp);
    strftime(buf, 100, "firstPlayed: %d %B %Y %I:%M %p", t);
    out << " " << buf << std::endl;
    timestamp = ps->lastPlayed;
    t = localtime(&timestamp);
    strftime(buf, 100, "lastPlayed: %d %B %Y %I:%M %p", t);
    out << " " << buf << std::endl;
    out << "playtime: " << ps->playtime << std::endl;
    out << "launches: " << ps->launches << std::endl;
#endif

    out.close();

    std::cout << "Read " << valid << " entries - dumped to playlog.txt under current homebrew application folder" << std::endl;
    std::cout << "Press any key to exit" << std::endl;
    consoleUpdate(NULL);

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    // Initialize pad
    PadState pad;
    padInitializeAny(&pad);

    // Initialize touch screen
    hidInitializeTouchScreen();

    while (appletMainLoop()){
        // Get key presses
        // Scan for input changes
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown){
            break;
        }
    }

    pdmqryExit();
    consoleExit(NULL);
    return 0;
}