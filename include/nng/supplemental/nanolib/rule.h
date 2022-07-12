#ifndef RULE_H
#define RULE_H
#include <stdint.h>

typedef enum {
	RULE_QOS,
	RULE_ID,
	RULE_TOPIC,
	RULE_CLIENTID,
	RULE_USERNAME,
	RULE_PASSWORD,
	RULE_TIMESTAMP,
	RULE_PAYLOAD_ALL,
	RULE_PAYLOAD_FIELD,
} rule_type;

typedef enum {
	RULE_CMP_NONE,              // compare type init value
	RULE_CMP_EQUAL,             // compare symbol '='
	RULE_CMP_UNEQUAL,           // compare symbol '!=' or '<>'
	RULE_CMP_GREATER,           // compare symbol '>'
	RULE_CMP_LESS,              // compare symbol '<'
	RULE_CMP_GREATER_AND_EQUAL, // compare symbol '>='
	RULE_CMP_LESS_AND_EQUAL,    // compare symbol '<='
} rule_cmp_type;

typedef enum {
	RULE_FORWORD_SQLITE,
	RULE_FORWORD_FDB,
	RULE_FORWORD_MYSOL,
	RULE_FORWORD_REPUB
} rule_forword_type;

typedef struct {
	char        **psa;      // payload string array, for multi level json
	char         *pas;      // payload field string or alias
	char         *filter;   // payload field related filter
	rule_cmp_type cmp_type; // payload field compare type
	bool          is_store; // payload value is store or not
	uint8_t       type;     // payload field value type
	void         *value;    // payload field value
} rule_payload;

typedef struct {
	bool    flag[9];
	bool    auto_inc;
	uint8_t type;
	void   *value;
	char  **key_arr;
} rule_key;

typedef struct {
	/* 
	** flag[0] == RULE_QOS,
	** flag[1] == RULE_ID,
	** flag[2] == RULE_TOPIC,
	** flag[3] == RULE_CLIENTID,
	** flag[4] == RULE_USERNAME,
	** flag[5] == RULE_PASSWORD,
	** flag[6] == RULE_TIMESTAMP,
	** flag[7] == RULE_PAYLOAD_ALL,
	** flag[8] == RULE_PAYLOAD_FIELD,
	*/
	bool           flag[9];        // if this field need to store
	rule_cmp_type  cmp_type[8]; 	// filter compare type
	rule_forword_type forword_type; // forword type
	char          *topic;          // topic parse from sql 'from'
	char          *as[8];          // if field string as a new string
	rule_payload **payload;        // this is for payload info
	char         **filter;         // filter parse from sql 'where'
	char *sqlite_table;
	rule_key *key;
	// TODO support create multi different table name
} rule;

typedef struct {
	/* 
	** 00000000 == OFF,
	** 00000001 == Sqlite ON,
	** 00000010 == Fdb ON,
	** 00000100 == MySOL ON
	** 00001000 == Repub ON
	** 00010000
	** 00100000
	** 01000000
	*/
	uint8_t option;
	/* 
	** rdb[0] == Sqlite
	** rdb[1] == Fdb
	** rdb[2] == MySOL
	** rdb[3] == RePub
	*/
	void *rdb[3]; 
	rule *rules;
	char *sqlite_db_path;
} conf_rule;

int rule_find_key(const char *str, size_t len);
char *rule_get_key_arr(char *p, rule_key *key);
bool rule_sql_parse(conf_rule *cr, char *sql);


#endif