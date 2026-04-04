#include "help.h"

static char const help_str[] =
    "\r\n"
    "Commands are Case Insensitive\r\n"
    "Use Backspace Key (BS) for Correction\r\n"
    "Use the DISP command to desplay all options\r\n"
    "Connect GPS for APRS Operation, (GP4/GP5/9600bps)\r\n"
    "Connect to Terminal for Command Interpreter, (USB serial or GP0/GP1/115200bps)\r\n"
    "\r\n"
    "Commands (with example):\r\n"
    "MYCALL (mycall jn1dff-2)\r\n"
    "UNPROTO (unproto jn1dff-14 v jn1dff-1) - 3 digis max\r\n"
    "BTEXT (btext Bob)-100 chars max\r\n"
    "BEACON (beacon every n)- n=0 is off and 1<n<60 mins\r\n"
    "MONitor (mon all,mon me, or mon off)\r\n"
    "DIGIpeat (digi on or digi off)\r\n"
    "MYALIAS (myalias RELAY)\r\n"
    "PERM (PERM)\r\n"
    "ECHO (echo on or echo off)\r\n"
    "GPS (gps $GPGGA or gps $GPGLL or gps $GPRMC)\r\n"
    "TRace (tr xmit or tr rcv) - For debugging only\r\n"
    "TXDELAY (txdelay n 0<n<201 n is number of delay flags to send)\r\n"
    "CALIBRATE (Calibrate Mode - Testing Only)\r\n"
    "CONverse (con)\r\n"
    "\r\n";

bool cmd_help(tty_t *ttyp, uint8_t *buf, int len)
{
    (void)buf;
    (void)len;

    tty_write_str(ttyp, help_str);

    return true;
}
