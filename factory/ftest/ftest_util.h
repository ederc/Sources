/* emacs edit mode for this file is -*- C++ -*- */
/* $Id: ftest_util.h,v 1.5 1997-10-01 12:25:34 schmidt Exp $ */

#ifndef INCL_FTEST_UTIL_H
#define INCL_FTEST_UTIL_H

//{{{ docu
//
// ftest_util.h - header to ftest_util.cc.
//
//}}}

enum ftestStatusT {
    Passed = 0, Failed, UndefinedResult
};

//{{{ enum ftestErrorT
enum ftestErrorT
{
    Ok, CommandlineError, EnvSyntaxError, CanFormSpecError,
    SignalError, TimeoutError = SignalError + 14
};
//}}}

extern int ftestCircle;
extern int ftestAlarm;

extern int ftestPrintFlag;
extern int ftestPrintResultFlag;

const char * ftestSkipBlancs ( const char * string );
void ftestError ( const ftestErrorT errno, const char * format ... );
void ftestPrint ( const char * longFormat, const char * shortFormat ... );

void ftestSignalCatch ();

void ftestSetName ( const char * execName, const char * algorithmName, const char * usage );

void ftestGetOpts ( const int argc, char ** argv, int & optind );
void ftestGetEnv ( const int, char ** argv, int & optind );

void ftestPrintTimer ( const long timer );
void ftestPrintCheck ( const ftestStatusT check );
void ftestPrintEnv ();

#endif /* ! INCL_FTEST_UTIL_H */
