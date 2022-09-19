if "%1" == "debug" (
    cl -EHsc -GR -nologo -D_DEBUG -D_WINSOCK_DEPRECATED_NO_WARNINGS -RTCsu -Z7 -W3 -w44355 -w44344 -w44251 -MDd caBroadcastRelay.cpp ws2_32.lib -link -debug
) else (
    cl -EHsc -GR -nologo -D _WINSOCK_DEPRECATED_NO_WARNINGS -Z7 -W3 -w44355 -w44344 -w44251 -MD caBroadcastRelay.cpp ws2_32.lib -link -debug
)
