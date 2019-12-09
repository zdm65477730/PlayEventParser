# PlayEventParser

Simple C++ homebrew for the Nintendo Switch which reads + dumps the "important bits" of PlayEvent.dat to the SD Card. This was created to help me work on NX-Activity-Log, but it's here should I need it again + for others to explore their play events.

## Format

Each relevant PlayEvent is dumped with the format:

APPLET: [title id] [event type] [user clock timestamp] [steady clock timestamp]

OR

ACCOUNT: [user id] [event type] [user clock timestamp] [steady clock timestamp]
