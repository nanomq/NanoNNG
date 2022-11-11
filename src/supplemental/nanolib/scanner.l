%{
#include <string.h>
#include "parser.h"

%}
/* %option debug */
%option noyywrap


Comment                 (("#"|"//")[^\n]*)
WhiteSpace              ([ \t\r\n]+)
Ignored                 {WhiteSpace}|{Comment}

EXP                     ([Ee][-+]?[0-9]+)
Letter                  [A-Za-z]
Unquoted                {Letter}[A-Za-z0-9_\.@%\-\ |\\"]*
Quoted                  \"([^\"]|\\\")*\"

Hex                     [0-9A-Fa-f]
Escape                  ["\\bfnrt]
UnicodeEscape           u{Hex}{Hex}{Hex}{Hex}
Char                    ([^\"]|\\{Escape}|\\{UnicodeEscape})
String                  "{Char}*"
MultilineChar           ([^"]|"[^"]|""[^"]|\\{Escape}|\\{UnicodeEscape})
MultilineString         """{MultilineChar}*"""

Punctuator              [{}\[\],:=]

Variable                \$\{{Unquoted}\}
MaybeVar                \$\{\?{Unquoted}\}

Digit                   [0-9]
Sign                    [+\-]
Integer                 {Sign}?({Digit}+)
Decimal                 {Integer}\.?[0-9]*{EXP}?

Percent                 {Digit}+%
Bytesize                {Digit}+(kb|KB|mb|MB|gb|GB)
Duration                {Digit}+(d|D|h|H|m|M|s|S|us|ms|MS)

Null                    null
True                    true 
False                   false
Lcurly                  "{"
Rcurly                  "}"
Lbrac                   "["
Rbrac                   "]"
Comma                   ","


%%
{Ignored}
{Lcurly}                { return LCURLY; }
{Rcurly}                { return RCURLY; } 
{Lbrac}                 { return LBRAC; } 
{Rbrac}                 { return RBRAC; } 
{Comma}                 { return COMMA; } 
{Punctuator}            { return PUNCT;}
{True}                  { return VTRUE; } 
{False}                 { return VFALSE; }
{Null}                  { return VNULL; } 
{Bytesize}              { yylval.strval = strdup(yytext); return BYTESIZE; }
{Percent}               { yylval.strval = strdup(yytext); return PERCENT; }
{Duration}              { yylval.strval = strdup(yytext); return DURATION; }
{Quoted}                { yylval.strval = strdup(yytext); return STRING; } 
{Unquoted}              { yylval.strval = strdup(yytext); return USTRING; } 
{Integer}               { yylval.intval  = atoi(yytext); return INTEGER; } 
{Decimal}               { yylval.floatval = atof(yytext); return DECIMAL ; } 
{MultilineString}       { /*TODO*/ printf("Multi line string: %s\n", yytext);}
{Variable}              { /*TODO*/ printf("Variable string: %s\n", yytext);}
{MaybeVar}              { /*TODO*/ printf("MaybeVar line string: %s\n", yytext);}
.                       { printf("Unrecognized %c\n", yytext[0]); }

%%


