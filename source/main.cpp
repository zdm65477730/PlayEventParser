#include <switch.h>
#include <fstream>
#include <iostream>
#include <time.h>

// Number of entries to process at one time
#define READ_ENTRIES 1000

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

int main(void){
    consoleInit(NULL);
    pdmqryInitialize();

    // File to write to
    std::ofstream out("/playlog.txt");
    std::cout << "Beginning dump/parsing..." << std::endl;
    consoleUpdate(NULL);

    u32 offset = 0;
    s32 total_out = 1;
    s32 valid = 0;

    PdmPlayEvent * events = new PdmPlayEvent[READ_ENTRIES];

    while (total_out > 0) {
        pdmqryQueryPlayEvent(offset, events, READ_ENTRIES, &total_out);
        offset += total_out;

        // Check if valid
        for (size_t i = 0; i < total_out; i++) {
            // Must be app/acc event
            switch (events[i].playEventType) {
                case PdmPlayEventType_Account: {
                    if (events[i].eventData.account.type == 2) {
                        continue;
                    }
                    out << "ACCOUNT: ";

                    // Print userID
                    u128 uID = events[i].eventData.account.userID[2];
                    uID = (uID << 32) | events[i].eventData.account.userID[3];
                    uID = (uID << 32) | events[i].eventData.account.userID[0];
                    uID = (uID << 32) | events[i].eventData.account.userID[1];
                    out << uID << " ";

                    // Print type
                    switch (events[i].eventData.account.type) {
                        case 0:
                            out << "Login";
                            break;
                        case 1:
                            out << "Logout";
                            break;
                    }
                    break;
                }

                case PdmPlayEventType_Applet: {
                    if (events[i].eventData.applet.logPolicy != PdmPlayLogPolicy_All) {
                        continue;
                    }
                    out << "APPLET: ";

                    // Print title ID
                    u64 tID = events[i].eventData.applet.titleID[0];
                    tID = (tID << 32) | events[i].eventData.applet.titleID[1];
                    out << std::hex << tID << std::dec << " ";

                    // Print type
                    switch (events[i].eventData.applet.eventType) {
                        case PdmAppletEventType_Launch:
                            out << "Launch";
                            break;
                        case PdmAppletEventType_Exit:
                        case PdmAppletEventType_Exit5:
                        case PdmAppletEventType_Exit6:
                            out << "Exit";
                            break;
                        case PdmAppletEventType_InFocus:
                            out << "In Focus";
                            break;
                        case PdmAppletEventType_OutOfFocus:
                        case PdmAppletEventType_OutOfFocus4:
                            out << "Out Focus";
                            break;
                    }
                    break;
                }

                default:
                    continue;
                    break;
            }

            // Print timestamps
            time_t timestamp = events[i].timestampUser;
            struct tm * t = localtime(&timestamp);
            char buf[100];
            strftime(buf, 100, "%d %B %Y %I:%M %p", t);
            out << " " << buf << " ";

            timestamp = events[i].timestampSteady;
            out << timestamp << std::endl;


            valid++;
        }
    }

    out.close();

    std::cout << "Read " << valid << " entries - dumped to sdmc:/playlog.txt" << std::endl;
    std::cout << "Press + to exit" << std::endl;
    consoleUpdate(NULL);

    delete[] events;

    while (appletMainLoop()){
        // Get key presses
        hidScanInput();
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS){
            break;
        }
    }

    pdmqryExit();
    consoleExit(NULL);
    return 0;
}