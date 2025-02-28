/* chromAlias.h was originally generated by the autoSql program, which also 
 * generated chromAlias.c and chromAlias.sql.  This header links the database and
 * the RAM representation of objects. */

#ifndef CHROMALIAS_H
#define CHROMALIAS_H

#define CHROMALIAS_NUM_COLS 3

extern char *chromAliasCommaSepFieldNames;

struct chromAlias
/* correspondence of UCSC chromosome names to refseq, genbank, and ensembl names */
    {
    struct chromAlias *next;  /* Next in singly linked list. */
    char *alias;	/* external name */
    char *chrom;	/* UCSC genome browser chromosome name */
    char *source;	/* comma separated list, when available: refseq,genbank,ensembl */
    };

void chromAliasStaticLoad(char **row, struct chromAlias *ret);
/* Load a row from chromAlias table into ret.  The contents of ret will
 * be replaced at the next call to this function. */

struct chromAlias *chromAliasLoad(char **row);
/* Load a chromAlias from row fetched with select * from chromAlias
 * from database.  Dispose of this with chromAliasFree(). */

struct chromAlias *chromAliasLoadAll(char *fileName);
/* Load all chromAlias from whitespace-separated file.
 * Dispose of this with chromAliasFreeList(). */

struct chromAlias *chromAliasLoadAllByChar(char *fileName, char chopper);
/* Load all chromAlias from chopper separated file.
 * Dispose of this with chromAliasFreeList(). */

#define chromAliasLoadAllByTab(a) chromAliasLoadAllByChar(a, '\t');
/* Load all chromAlias from tab separated file.
 * Dispose of this with chromAliasFreeList(). */

struct chromAlias *chromAliasCommaIn(char **pS, struct chromAlias *ret);
/* Create a chromAlias out of a comma separated string. 
 * This will fill in ret if non-null, otherwise will
 * return a new chromAlias */

void chromAliasFree(struct chromAlias **pEl);
/* Free a single dynamically allocated chromAlias such as created
 * with chromAliasLoad(). */

void chromAliasFreeList(struct chromAlias **pList);
/* Free a list of dynamically allocated chromAlias's */

void chromAliasOutput(struct chromAlias *el, FILE *f, char sep, char lastSep);
/* Print out chromAlias.  Separate fields with sep. Follow last field with lastSep. */

#define chromAliasTabOut(el,f) chromAliasOutput(el,f,'\t','\n');
/* Print out chromAlias as a line in a tab-separated file. */

#define chromAliasCommaOut(el,f) chromAliasOutput(el,f,',',',');
/* Print out chromAlias as a comma separated list including final comma. */

void chromAliasJsonOutput(struct chromAlias *el, FILE *f);
/* Print out chromAlias in JSON format. */

/* -------------------------------- End autoSql Generated Code -------------------------------- */


void chromAliasSetup(char *database);
/* Read in the chromAlias file/table for this database. */

char *chromAliasFindNative(char *name);
/* Find the native seqName for a given alias. */

struct slName *chromAliasFindAliases(char *seqName);
/* Get the list of aliases for this sequence name. */

char *chromAliasFindSingleAlias(char *seqName, char *authority);
/* Find the aliases for a given seqName from a given authority. */

char *chromAliasGetDisplayChrom(char *db, struct cart *cart, char *seqName);
/* Return the sequence name to display based on the database and cart. */
#endif /* CHROMALIAS_H */
