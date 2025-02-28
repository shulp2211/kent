/* tablesTables - this module deals with two types of tables SQL tables in a database,
 * and fieldedTable objects in memory. It has routines to do sortable, filterable web
 * displays on tables. */

#include "common.h"
#include "hash.h"
#include "obscure.h"
#include "linefile.h"
#include "jksql.h"
#include "jsHelper.h"
#include "sqlSanity.h"
#include "fieldedTable.h"
#include "cheapcgi.h"
#include "htmshell.h"
#include "web.h"
#include "cart.h"
#include "facetField.h"
#include "tablesTables.h"
#include "csv.h"

struct fieldedTable *fieldedTableFromDbQuery(struct sqlConnection *conn, char *query)
/* Return fieldedTable from a database query */
{
struct sqlResult *sr = sqlGetResult(conn, query);
char **fields;
int fieldCount = sqlResultFieldArray(sr, &fields);
struct fieldedTable *table = fieldedTableNew(query, fields, fieldCount);
char **row;
int i = 0;
while ((row = sqlNextRow(sr)) != NULL)
    fieldedTableAdd(table, row, fieldCount, ++i);
sqlFreeResult(&sr);
return table;
}

struct fieldedTable *fieldedTableAndCountsFromDbQuery(struct sqlConnection *conn, char *query, int limit, int offset, 
    char *visibleFields, struct facetField ***pFfArray, int *pResultCount)
/* Return fieldedTable from a database query and also fetch use and select counts */
{
struct sqlResult *sr = sqlGetResult(conn, query);
char **fields;
int fieldCount = sqlResultFieldArray(sr, &fields);
struct facetField **ffArray;
AllocArray(ffArray, fieldCount);
struct fieldedTable *table = fieldedTableNew(query, fields, fieldCount);

struct facetField *ffList = facetFieldsFromSqlTableInit(fields, fieldCount, visibleFields, ffArray);

char **row;
int i = 0;
int id = 0;
char *nullVal = "n/a"; 
/* Scan through result saving it in list. */
while ((row = sqlNextRow(sr)) != NULL)
    {
    if (perRowFacetFields(fieldCount, row, nullVal, ffArray))
	{
	if ((i >= offset) && (i < offset+limit))
	    fieldedTableAdd(table, row, fieldCount, ++id);
	++i;
	}
    }
facetFieldsFromSqlTableFinish(ffList, facetValCmpSelectCountDesc);
sqlFreeResult(&sr);
*pFfArray = ffArray;
*pResultCount = i;
return table;
}

static void showTableFilterInstructionsEtc(struct fieldedTable *table, 
    char *pluralInstructions, struct  fieldedTableSegment *largerContext, void (*addFunc)(int),
    char *visibleFacetList, char *varPrefix)
/* Print instructional text, and basic summary info on who passes filter, and a submit
 * button just in case user needs it */
{
/* Print info on matching */
int matchCount = slCount(table->rowList);
if (largerContext != NULL)  // Need to page?
     matchCount = largerContext->tableSize;

printf("<input class='btn btn-secondary' type='submit' name='submit' id='submit' value='Search'>");

printf("&nbsp&nbsp;");
printf("<input class='btn btn-secondary' type='button' id='clearButton' VALUE=\"Clear Search\">");
char jsText[1024];
safef(jsText, sizeof(jsText),
    "$(':input').not(':button, :submit, :reset, :hidden, :checkbox, :radio').val('');\n"
    "$('[name=%s_page]').val('1');\n"
    "$('#submit').click();\n",  varPrefix);
jsOnEventById("click", "clearButton", jsText);

printf("<br>");

printf("%d&nbsp;%s&nbsp;found. ", matchCount, pluralInstructions);

if (addFunc)
    addFunc(matchCount);

if (!visibleFacetList)
    {
    printf("<BR>\n");
    printf("You can further filter search results field by field below. ");    
    printf("Wildcard * and ? characters are allowed in text fields. ");
    printf("&GT;min or &LT;max are allowed in numerical fields.<BR>\n");
    }
}

static void printSuggestScript(char *id, struct slName *suggestList)
/* Print out a little javascript to wrap auto-suggester around control with given ID */
{
struct dyString *dy = dyStringNew(256);
dyStringPrintf(dy,"$(document).ready(function() {\n");
dyStringPrintf(dy,"  $('#%s').autocomplete({\n", id);
dyStringPrintf(dy,"    delay: 100,\n");
dyStringPrintf(dy,"    minLength: 0,\n");
dyStringPrintf(dy,"    source: [");
char *separator = "";
struct slName *suggest;
for (suggest = suggestList; suggest != NULL; suggest = suggest->next)
    {
    dyStringPrintf(dy,"%s\"", separator);
    dyStringAppendEscapeQuotes(dy, suggest->name, '"', '\\');
    dyStringPrintf(dy, "\"");
    separator = ",";
    }
dyStringPrintf(dy,"]\n");
dyStringPrintf(dy,"    });\n");
dyStringPrintf(dy,"});\n");
jsInline(dy->string);
dyStringFree(&dy);
}

#ifdef NOT_CURRENTLY_USED
static void printWatermark(char *id, char *watermark)
/* Print light text filter prompt as watermark. */
{
jsInlineF(
"$(function() {\n"
"  $('#%s').watermark(\"%s\");\n"
"});\n", id, watermark);
}
#endif

static void resetPageNumberOnChange(char *id, char *varPrefix)
/* On change, reset page number to 1. */
{
jsInlineF(
"$(function() {\n"
" $('form').delegate('#%s','change keyup paste',function(e){\n"
"  $('[name=%s_page]').val('1');\n"
" });\n"
"});\n"
, id, varPrefix);
}


static void showTableFilterControlRow(struct fieldedTable *table, struct slName *visibleFields,
    struct cart *cart, char *varPrefix, int maxLenField, struct hash *suggestHash)
/* Assuming we are in table already drow control row.
 * The suggestHash is keyed by field name.  If something is there we'll assume
 * it's value is slName list of suggestion values */
{
/* Include javascript and style we need  */
printf("<link rel='stylesheet' href='//code.jquery.com/ui/1.12.1/themes/base/jquery-ui.css'>\n");
printf("<script src='https://code.jquery.com/ui/1.12.1/jquery-ui.js'></script>\n");

printf("<tr>");
struct slName *el;
for (el = visibleFields; el != NULL; el = el->next)
    {
    char *field = el->name;
    int fieldIx = fieldedTableFindFieldIx(table, field);
    if (fieldIx >= 0)
	{
	char varName[256];
	safef(varName, sizeof(varName), "%s_f_%s", varPrefix, field);
	printf("<td>");

	/* Approximate size of input control in characters */
	int size = fieldedTableMaxColChars(table, fieldIx);
	if (size > maxLenField)
	    size = maxLenField;

	/* Print input control getting previous value from cart.  Set an id=
	 * so auto-suggest can find this control. */
	char *oldVal = cartUsualString(cart, varName, "");
	printf("<INPUT TYPE=TEXT NAME=\"%s\" id=\"%s\" SIZE=%d",
	    varName, varName, size+1);
	if (isEmpty(oldVal))
	    printf(" placeholder=\" filter \">\n");
	else
	    printf(" value=\"%s\">\n", oldVal);

	/* Write out javascript to reset page number to 1 if filter changes */
	resetPageNumberOnChange(varName, varPrefix);

	/* Set up the auto-suggest list for this filter */
	if (suggestHash != NULL)
	    {
	    struct slName *suggestList = hashFindVal(suggestHash, field);
	    if (suggestList != NULL)
		{
		printSuggestScript(varName, suggestList);
		}
	    }
	printf("</td>\n");
	}
    }


printf("</TR>");
}

static void showTableSortingLabelRow(struct fieldedTable *table, struct slName *visibleFields, 
    struct cart *cart, char *varPrefix, char *returnUrl)
/* Put up the label row with sorting fields attached.  ALso actually sort table.  */
{
/* Get order var */
char orderVar[256];
safef(orderVar, sizeof(orderVar), "%s_order", varPrefix);
char *orderFields = cartUsualString(cart, orderVar, "");

char pageVar[64];
safef(pageVar, sizeof(pageVar), "%s_page", varPrefix);

/* Print column labels */
struct slName *vis;
for (vis = visibleFields; vis != NULL; vis = vis->next)
    {
    if (fieldedTableFindFieldIx(table, vis->name) != -1)
	{
	printf("<td>");
	printf("<A class=\"topbar\" HREF=\"");
	printf("%s", returnUrl);
	printf("&%s=1", pageVar);
	printf("&%s=", orderVar);
	char *field = vis->name;
	if (!isEmpty(orderFields) && sameString(orderFields, field))
	    printf("-");
	printf("%s", field);
	printf("\">");
	printf("%s", field);
	if (!isEmpty(orderFields))
	    {
	    char *s = orderFields;
	    boolean isRev = (s[0] == '-');
	    if (isRev)
		++s;
	    if (sameString(field, s))
		{
		if (isRev)
		    printf("&uarr;");
		else
		    printf("&darr;");
		}
	    }
	printf("</A>");
	printf("</td>\n");
	}
    }

/* Sort on field */
if (!isEmpty(orderFields))
    {
    boolean doReverse = FALSE;
    char *field = orderFields;
    if (field[0] == '-')
        {
	field += 1;
	doReverse = TRUE;
	}
    fieldedTableSortOnField(table, field, doReverse);
    }
}

static void showTableDataRows(struct fieldedTable *table, struct slName *visibleFields,
    int pageSize, int maxLenField,
    struct hash *tagOutputWrappers, void *wrapperContext)
/* Render data rows into HTML */
{
/* Look up visible fields in table */
int visFieldCount = slCount(visibleFields);
int visIx[visFieldCount];
int i;
struct slName *el = visibleFields;;
for (i=0; i<visFieldCount; ++i, el = el->next)
    visIx[i] = fieldedTableFindFieldIx(table, el->name);

/* Figure out numerical ones */
int count = 0;
struct fieldedRow *row;
boolean isNum[visFieldCount];
for (i=0; i<visFieldCount; ++i)
    {
    int vix = visIx[i];
    if (vix >= 0)
	isNum[i] = fieldedTableColumnIsNumeric(table, visIx[i]);
    else
        isNum[i] = FALSE;
    }

for (row = table->rowList; row != NULL; row = row->next)
    {
    if (++count > pageSize)
         break;
    printf("<TR>\n");
    int fieldIx = 0;
    int i;
    for (i=0; i<visFieldCount; ++i)
	{
	fieldIx = visIx[i];
	if (fieldIx >= 0)
	    {
	    char shortVal[maxLenField+1];
	    char *longVal = emptyForNull(row->row[fieldIx]);
	    char *val = longVal;
	    int valLen = strlen(val);
	    if (maxLenField > 0 && maxLenField < valLen)
		{
		if (valLen > maxLenField)
		    {
		    memcpy(shortVal, val, maxLenField-3);
		    shortVal[maxLenField-3] = 0;
		    strcat(shortVal, "...");
		    val = shortVal;
		    }
		}
	    if (isNum[fieldIx]) // vacuous, but left it just in case we want 
				// to do different stuff to numbers later
		printf("<td>");
	    else
		printf("<td>");
	    boolean printed = FALSE;
	    if (tagOutputWrappers != NULL && !isEmpty(val))
		{
		char *field = table->fields[fieldIx];
		webTableOutputWrapperType *printer = hashFindVal(tagOutputWrappers, field);
		if (printer != NULL)
		    {
		    printer(table, row, field, longVal, val, wrapperContext);
		    printed = TRUE;
		    }
		
		}
	    if (!printed)
		printf("%s", val);
	    printf("</td>\n");
	    }
	}
    printf("</TR>\n");
    }
}

static void showTablePaging(struct fieldedTable *table, struct cart *cart, char *varPrefix,
    struct fieldedTableSegment *largerContext, int pageSize)
/* If larger context exists and is bigger than current display, then draw paging controls. */
{
/* Handle paging if any */
if (largerContext != NULL)  // Need to page?
     {
     if (pageSize < largerContext->tableSize)
	{
	int curPage = largerContext->tableOffset/pageSize;
	int totalPages = (largerContext->tableSize + pageSize - 1)/pageSize;


	char id[256];
	if ((curPage + 1) > 1)
	    {
	    // first page
	    safef(id, sizeof id, "%s_first", varPrefix);
	    printf("<a href='#' id='%s'>&#9198;</a>", id);
	    jsOnEventByIdF("click", id, 
		"$('[name=%s_page]').val('1');\n"
		"event.target.closest('form').submit();\n"
		, varPrefix);
	    printf("&nbsp;&nbsp;&nbsp;");

	    // prev page
	    safef(id, sizeof id, "%s_prev", varPrefix);
	    printf("<a href='#' id='%s'>&#9194;</a>", id);
	    jsOnEventByIdF("click", id, 
		"$('[name=%s_page]').val('%d');\n"
		"event.target.closest('form').submit();\n"
		, varPrefix, (curPage+1)-1);
	    printf("&nbsp;&nbsp;&nbsp;");
	    }


	printf("Displaying page ");

	char pageVar[64];
	safef(pageVar, sizeof(pageVar), "%s_page", varPrefix);
	cgiMakeIntVar(pageVar, curPage+1, 3);

	printf(" of %d", totalPages);

	if ((curPage + 1) < totalPages)
	    {
	    // next page
	    printf("&nbsp;&nbsp;&nbsp;");
	    safef(id, sizeof id, "%s_next", varPrefix);
	    printf("<a href='#' id='%s'>&#9193;</a>", id);
	    jsOnEventByIdF("click", id, 
		"$('[name=%s_page]').val('%d');\n"
		"event.target.closest('form').submit();\n"
		, varPrefix, (curPage+1)+1);

	    // last page
	    printf("&nbsp;&nbsp;&nbsp;");
	    safef(id, sizeof id, "%s_last", varPrefix);
	    printf("<a href='#' id='%s'>&#9197;</a>", id);
	    jsOnEventByIdF("click", id, 
		"$('[name=%s_page]').val('%d');\n"
		"event.target.closest('form').submit();\n"
		, varPrefix, totalPages);

	    }
	}
     }
}

static void createSelfId(char *varPrefix, char *fieldName, 
	char *val, char *selfId, int selfIdSize)
{
if (val == NULL)
    safef(selfId, selfIdSize, "%s_self_a_%s", varPrefix, fieldName);
else
    safef(selfId, selfIdSize, "%s_self_a_%s_%s", varPrefix, fieldName, val);
subChar(selfId, ' ', '_');
}

void webFilteredFieldedTable(struct cart *cart, struct fieldedTable *table, 
    char *visibleFieldList, char *returnUrl, char *varPrefix,
    int maxLenField, struct hash *tagOutputWrappers, void *wrapperContext,
    boolean withFilters, char *pluralInstructions, 
    int pageSize, int facetUsualSize,
    struct fieldedTableSegment *largerContext, struct hash *suggestHash, 
    struct facetField **ffArray, char *visibleFacetList,
    void (*addFunc)(int), boolean facetMergeOk )
/* Show a fielded table that can be sorted by clicking on column labels and optionally
 * that includes a row of filter controls above the labels .
 * The maxLenField is maximum character length of field before truncation with ...
 * Pass in 0 for no max. */
{
if (strchr(returnUrl, '?') == NULL)
     errAbort("Expecting returnUrl to include ? in showFieldedTable\nIt's %s", returnUrl);

if (pluralInstructions != NULL)
    showTableFilterInstructionsEtc(table, pluralInstructions, largerContext, addFunc, 
	    visibleFacetList, varPrefix);

if (visibleFacetList)
    {
    // Show top bar with quick-deselects for selected facet values
    //  as well a clear restriction button that cleans out _filter cart var. 

    struct dyString *facetBar = dyStringNew(1024);
    char filterVar[256];
    safef(filterVar, sizeof(filterVar), "%s_filter", varPrefix);

    char *where = cartUsualString(cart, filterVar, "");


    boolean gotSelected = FALSE;
    boolean anyMerged = FALSE;

    struct slName *visList = slNameListFromComma(visibleFacetList);
    struct slName *vis;
    for (vis = visList; vis != NULL; vis = vis->next)
	{
	int fIx = fieldedTableFindFieldIx(table, vis->name);
	if (fIx >= 0)
	    {
	    struct facetField *field = ffArray[fIx];
	    if (!field->allSelected)
		{
		gotSelected = TRUE;
		htmlDyStringPrintf(facetBar, 
		    "<span class='card facet-card' style='display: inline-block;'>"
		    "<span class='card-body'>\n");
		htmlDyStringPrintf(facetBar, "<dt style='display: inline-block;'>\n");
		htmlDyStringPrintf(facetBar, "<h6 class='card-title'>%s</h6></dt>\n", 
		    field->fieldName);

		struct facetVal *val;

		// Sort values alphabetically
		// Make a copy to not disturb the original order 
		struct facetVal *valListCopy = facetsClone(field->valList);
		slSort(&valListCopy, facetValCmp);
		
		for (val = valListCopy; val; val=val->next)
		    {
		    boolean specificallySelected = (val->selected && !field->allSelected);
		    if (specificallySelected)
			{
			char *op = "remove";
			htmlDyStringPrintf(facetBar, 
			    "<dd class=\"facet\" style='display: inline-block;'>\n");
			htmlDyStringPrintf(facetBar, 
			    "<input type=checkbox value=%s class=ttFsCheckBox %s>&nbsp;",
			    specificallySelected ? "true" : "false", 
			    specificallySelected ? "checked" : "");
			htmlDyStringPrintf(facetBar, "<a href='%s"
				"&%s_facet_op=%s|url|"
				"&%s_facet_fieldName=%s|url|"
				"&%s_facet_fieldVal=%s|url|"
				"&%s_page=1'"
				">",
			    returnUrl, varPrefix,
			    op, varPrefix, field->fieldName, varPrefix, val->val, varPrefix
			    );
			htmlDyStringPrintf(facetBar, "%s (%d)</a>", 
			    naForEmpty(val->val), val->selectCount);
			htmlDyStringPrintf(facetBar, "</dd>\n");
			}
		    }
		slFreeList(&valListCopy);
		
		htmlDyStringPrintf(facetBar, "</span></span>\n");
		} }
	else
	    {
	    anyMerged = TRUE;
	    htmlDyStringPrintf(facetBar, 
		"<span class='card facet-card' style='display: inline-block;'>"
		"<span class='card-body'>\n");
	    htmlDyStringPrintf(facetBar, "<dt style='display: inline-block;'>\n");
	    htmlDyStringPrintf(facetBar, "<h6 class='card-title'>%s</h6></dt>\n", 
		vis->name);
	    htmlDyStringPrintf(facetBar, " <a class='btn btn-secondary' href='%s"
		    "&%s_facet_op=%s|none|"
		    "&%s_facet_fieldName=%s|url|"
		    "&%s_facet_fieldVal=%s|url|"
		    "&%s_page=1' "
		    ">", 
		    returnUrl, varPrefix, "unmerge", varPrefix, vis->name, 
		    varPrefix, "", varPrefix);
	    htmlDyStringPrintf(facetBar, " %s", "unmerge");
	    htmlDyStringPrintf(facetBar, "</a>");
	    htmlDyStringPrintf(facetBar, "</span></span>\n");
	    }
	}

    if (!isEmpty(where) || gotSelected || anyMerged)
        {
	printf("<div>\n");
        }

    if (!isEmpty(where))
	{
	// left column
        
	printf("Restricting files to where %s. ", where);

	printf("&nbsp&nbsp;");
	printf("<input class='btn btn-secondary' type='button' id='clearRestrictionButton' VALUE=\"Clear Restriction\">");
	char jsText[1024];
	safef(jsText, sizeof(jsText),
	    "$('[name=%s_page]').val('1');\n"
	    "$('[name=clearRestriction]').val('1');\n"
	    "$('#submit').click();\n", varPrefix);
	jsOnEventById("click", "clearRestrictionButton", jsText);

	printf("<br>");
        }

    if (gotSelected || anyMerged)
	{
	// reset all facet value selections button
	char *op = "resetAll";
	htmlPrintf("<a class='btn btn-secondary' href='%s"
	    "&%s_facet_op=%s|none|"
	    "&%s_facet_fieldName=%s|url|"
	    "&%s_facet_fieldVal=%s|url|"
	    "&%s_page=1' "
		">%s</a>\n",
		returnUrl, varPrefix, op, varPrefix, "", varPrefix, "", varPrefix, "Clear All"
	    );

	printf("<dl style='display: inline-block;'>\n");
	printf("%s\n", facetBar->string);
	printf("</dl>\n");
	}

    if (!isEmpty(where) || gotSelected || anyMerged)
	printf("</div><br>\n");

    dyStringFree(&facetBar);
    }

printf("<div class='row'>\n"); // parent container

if (visibleFacetList)
    {
    // left column
    printf("<div class='col-xs-6 col-sm-4 col-md-4 col-lg-3 col-xl-3'>\n");

    struct slName *visList = slNameListFromComma(visibleFacetList);
    struct slName *vis;
    for (vis = visList; vis != NULL; vis = vis->next)
	{
	char *fieldName = vis->name;
	char selfId[256];
	createSelfId(varPrefix, fieldName, NULL, selfId, sizeof(selfId));

	/* Work on facet field label line */
	htmlPrintf("<div id=\"%s\" class='card facet-card'><div class='card-body'>\n", selfId);
	htmlPrintf("<h6 class='card-title'>%s",vis->name);

	int f = fieldedTableFindFieldIx(table, fieldName);
	char *op = "unmerge";
	struct facetField *field = NULL;
	if (f >= 0)
	    {
	    field = ffArray[f];
	    if (!field->isMerged)
	        op = "merge";
	    }

	/* Write merge/unmerge link and number of categories */
	if (facetMergeOk)
	    {
	    char selfId[256];
	    createSelfId(varPrefix, fieldName, NULL, selfId, sizeof(selfId));
	    htmlPrintf("<span style='float:right'>");
	    htmlPrintf("<a class='btn btn-secondary' href='%s"
		    "&%s_facet_op=%s|none|"
		    "&%s_facet_fieldName=%s|url|"
		    "&%s_facet_fieldVal=%s|url|"
		    "&%s_page=1#%s' "
		    ">", 
		    returnUrl, varPrefix, op, varPrefix, fieldName, 
		    varPrefix, "", varPrefix, selfId);
	    htmlPrintf("%s", op);

	    if (field != NULL && sameString(op, "merge"))
		{
		if (!field->allSelected)
		    {
		    int selectedFieldCount = facetFieldCountSelected(field);
		    htmlPrintf(" %d", selectedFieldCount);
		    }
		}
	    htmlPrintf("</a></span>");
	    }

	/* CLose up facet field label line */
	htmlPrintf("</h6><dl>\n");

	if (field != NULL)
	    {
	    struct facetVal *val;
	    if (!field->allSelected)  // add reset facet link
		{
		char *op = "reset";
		htmlPrintf("<dd><a class='btn btn-secondary' href='%s"
			"&%s_facet_op=%s|url|"
			"&%s_facet_fieldName=%s|url|"
			"&%s_facet_fieldVal=%s|url|"
			"&%s_page=1' "
			">%s</a></dd>\n",
		    returnUrl, varPrefix, op, 
		    varPrefix, field->fieldName, varPrefix, "", varPrefix,
		    "Clear"
		    );
		}

	    int valuesShown = 0;
	    int valuesNotShown = 0;
	    if (field->showAllValues)  // Sort alphabetically if they want all values 
		{
		slSort(&field->valList, facetValCmp);
		}
	    int extraAnchorPeriod = 15;
	    int extraAnchorPos = 0;
	    for (val = field->valList; val; val=val->next)
		{
		boolean specificallySelected = (val->selected && !field->allSelected);
		if ((val->selectCount > 0 && 
		    (field->showAllValues || valuesShown < facetUsualSize) && 
		    !field->isMerged)
		    || specificallySelected)
		    {
		    ++valuesShown;
		    ++extraAnchorPos;
		    char *op = "add";
		    if (specificallySelected)
			op = "remove";
		    printf("<dd class=\"facet\"");
		    if (extraAnchorPos >= extraAnchorPeriod)
			{
			char selfId[256];
			createSelfId(varPrefix, vis->name, val->val, selfId, sizeof(selfId));
			printf(" id=\"%s\"", selfId);
			extraAnchorPos= 0;
			}
		    printf(">\n");
		    htmlPrintf("<input type=checkbox value=%s class=ttFsCheckBox %s>&nbsp;",
			specificallySelected ? "true" : "false", 
			specificallySelected ? "checked" : "");
		    htmlPrintf("<a href='%s"
			    "&%s_facet_op=%s|none|"
			    "&%s_facet_fieldName=%s|url|"
			    "&%s_facet_fieldVal=%s|url|"
			    "&%s_page=1#%s' "
			    ">",
			returnUrl, varPrefix,
			op, varPrefix, field->fieldName, varPrefix, val->val, varPrefix, selfId
			);
		    htmlPrintf("%s (%d)</a>", naForEmpty(val->val), val->selectCount);
		    printf("</dd>\n");
		    }
		else if (val->selectCount > 0)
		    {
		    ++valuesNotShown;
		    }
		}

	    // show "See More" link when facet has lots of values
	    if (valuesNotShown > 0 && !field->isMerged)
		{
		char *op = "showAllValues";
		htmlPrintf("<dd><a href='%s"
			"&%s_facet_op=%s|url|"
			"&%s_facet_fieldName=%s|url|"
			"&%s_facet_fieldVal=%s|url|"
			"&%s_page=1#%s' "
			">See %d More</a></dd>\n",
		    returnUrl, varPrefix, op, 
		    varPrefix, field->fieldName, varPrefix, "", 
		    varPrefix, selfId, valuesNotShown
		    );
		}

	    // show "See Fewer" link when facet has lots of values
	    if (field->showAllValues && valuesShown >= facetUsualSize)
		{
		char selfId[256];
		createSelfId(varPrefix, vis->name, NULL, selfId, sizeof(selfId));
		char *op = "showSomeValues";
		htmlPrintf("<dd><a href='%s"
			"&%s_facet_op=%s|url|"
			"&%s_facet_fieldName=%s|url|"
			"&%s_facet_fieldVal=%s|url|"
			"&%s_page=1#%s' "
			">%s</a></dd>\n",
		    returnUrl, varPrefix, op, varPrefix, field->fieldName, varPrefix, "", varPrefix,
		    selfId, "See Fewer"
		    );
		}
	    }
	htmlPrintf("</div></div>\n");
	}
    printf("</div>\n");
    // Clicking a checkbox is actually a click on the following link
    jsInlineF(
	"$(function () {\n"
	"  $('.ttFsCheckBox').click(function() {\n"
	"    this.nextSibling.nextSibling.click();\n"
	"  });\n"
	"});\n");
    }

// start right column, if there are two columns
if (visibleFacetList)
    printf("<div class='col-xs-6 col-sm-8 col-md-8 col-lg-9 col-xl-9'>\n");
else
    printf("<div class='col-12'>\n");
    
printf("  <div>\n");
if (visibleFieldList != NULL)
    {
    struct slName *fieldList = slNameListFromComma(visibleFieldList);
    printf("    <table class=\"table table-striped table-bordered table-sm text-nowrap\">\n");

    /* Draw optional filters cells ahead of column labels*/
    printf("<thead>\n");
    if (withFilters)
	showTableFilterControlRow(table, fieldList, cart, varPrefix, maxLenField, suggestHash);
    showTableSortingLabelRow(table, fieldList, cart, varPrefix, returnUrl);
    printf("</thead>\n");

    printf("<tbody>\n");
    showTableDataRows(table, fieldList, pageSize, maxLenField, tagOutputWrappers, wrapperContext);
    printf("</tbody>\n");
    printf("</table>\n");
    printf("</div>");
    if (largerContext != NULL)
	showTablePaging(table, cart, varPrefix, largerContext, pageSize);

    }

if (visibleFacetList) // close right column, if there are two columns
    printf("</div>");

printf("</div>\n"); //close parent container
}

void webSortableFieldedTable(struct cart *cart, struct fieldedTable *table, 
    char *returnUrl, char *varPrefix,
    int maxLenField, struct hash *tagOutputWrappers, void *wrapperContext)
/* Display all of table including a sortable label row.  The tagOutputWrappers
 * is an optional way to enrich output of specific columns of the table.  It is keyed
 * by column name and has for values functions of type webTableOutputWrapperType. */
{
struct dyString *visibleFacetList = dyStringNew(256); 
int i;
for (i = 0; i < table->fieldCount; ++i)
    {
    if (i > 0) dyStringPrintf(visibleFacetList, ",");
    dyStringPrintf(visibleFacetList, "%s", table->fields[i]);
    }
webFilteredFieldedTable(cart, table, visibleFacetList->string, returnUrl, varPrefix, 
    maxLenField, tagOutputWrappers, wrapperContext,
    FALSE, NULL, 
    slCount(table->rowList), 
    0, NULL, NULL, NULL, NULL, NULL, FALSE);
}


void webTableBuildQuery(struct cart *cart, char *from, char *initialWhere, 
    char *varPrefix, char *fields, boolean withFilters, 
    struct dyString **retQuery, struct dyString **retWhere)
/* Construct select, from and where clauses in query, keeping an additional copy of where 
 * Returns the SQL query and the SQL where expression as two dyStrings (need to be freed)  */
{
struct dyString *query = dyStringNew(0);
struct dyString *where = dyStringNew(0);
struct slName *field, *fieldList = commaSepToSlNames(fields);
boolean gotWhere = FALSE;
sqlCkIl(fieldsSafe,fields)
sqlCkIl(fromSafe,from)

// from can be a list of tables if joining
sqlDyStringPrintf(query, "select %-s from %-s", fieldsSafe, fromSafe);
if (!isEmpty(initialWhere))
    {
    sqlDyStringPrintf(where, " where ");
    sqlDyStringPrintf(where, "%-s", initialWhere);
    gotWhere = TRUE;
    }

/* If we're doing filters, have to loop through the row of filter controls */
if (withFilters)
    {
    for (field = fieldList; field != NULL; field = field->next)
        {
	char varName[128];
	safef(varName, sizeof(varName), "%s_f_%s", varPrefix, field->name);
	char *val = trimSpaces(cartUsualString(cart, varName, ""));
	if (!isEmpty(val))
	    {
	    if (gotWhere)
		sqlDyStringPrintf(where, " and ");
	    else
		{
	        sqlDyStringPrintf(where, " where ");
		gotWhere = TRUE;
		}
	    if (anyWild(val))
		{
		char *converted = sqlLikeFromWild(val);
		sqlDyStringPrintf(where, "%s like '%s'", field->name, converted);
		freez(&converted);
		}
	    else if (val[0] == '>' || val[0] == '<')
		{
		char *remaining = val+1;
		if (remaining[0] == '=')
		    {
		    remaining += 1;
		    }
		remaining = skipLeadingSpaces(remaining);
		if (isNumericString(remaining))
		    {
		    sqlDyStringPrintf(where, "%s ", field->name);
		    if (val[0] == '>')
			sqlDyStringPrintf(where, ">");
		    if (val[0] == '<')
			sqlDyStringPrintf(where, "<");
		    if (val[1] == '=')
			sqlDyStringPrintf(where, "=");
		    sqlDyStringPrintf(where, "%s", remaining);
		    }
		else
		    {
		    warn("Filter for %s doesn't parse:  %s", field->name, val);
		    sqlDyStringPrintf(where, "%s is not null", field->name); // Let query continue
		    }
		}
	    else
		{
		sqlDyStringPrintf(where, "%s = '%s'", field->name, val);
		}
	    }
	}
    }
if (!isEmpty(where->string))
    sqlDyStringPrintf(query, "%-s", where->string);  // trust

/* We do order here so as to keep order when working with tables bigger than a page. */
char orderVar[256];
safef(orderVar, sizeof(orderVar), "%s_order", varPrefix);
char *orderFields = cartUsualString(cart, orderVar, "");
if (!isEmpty(orderFields))
    {
    if (orderFields[0] == '-')
	sqlDyStringPrintf(query, " order by %s desc", orderFields+1);
    else
	sqlDyStringPrintf(query, " order by %s", orderFields);
    }

// return query and where expression
*retQuery = query;
*retWhere = where;
}

struct dyString *fuseCsvFields(struct sqlConnection *conn, char *tables, 
    char *firstCsv, char *secondCsv)
/* Return a list that is firstCsv followed by any fields in secondCsv not already in firstCsv
 *      "a,b,c,d",  "b,f,d,e"   yeilds "a,b,c,d,f,e"
 * order is preserved in firstCsv and when possible in second */
{
struct hash *uniq = hashNew(0);
struct dyString *result = dyStringNew(0);

/* Add everything in aList to both hash and result */
struct slName *el;
struct slName *aList = slNameListFromComma(firstCsv);
for (el = aList; el != NULL; el = el->next)
    {
    if (sqlColumnExistsInTablesList(conn, tables, el->name))
	csvEscapeAndAppend(result, el->name);
    hashAdd(uniq, el->name, NULL);
    }

/* Only add bList if it's not in there already and it is in database */
struct slName *bList = slNameListFromComma(secondCsv);
for (el = bList; el != NULL; el = el->next)
    {
    if (!hashLookup(uniq, el->name))
        {
	if (sqlColumnExistsInTablesList(conn, tables, el->name))
	    csvEscapeAndAppend(result, el->name);
	hashAdd(uniq, el->name, NULL);
	}
    }

/* Clean up and return with result */
slFreeList(&aList);
slFreeList(&bList);
hashFree(&uniq);
return result;
}

void webFilteredSqlTable(struct cart *cart,    /* User set preferences here */
    struct sqlConnection *conn,		       /* Connection to database */
    char *fields, char *from, char *initialWhere,  /* Our query in three parts, from can be a table list if joining */
    char *returnUrl, char *varPrefix,	       /* Url to get back to us, and cart var prefix */
    int maxFieldWidth,			       /* How big do we let fields get in characters */
    struct hash *tagOutWrappers,	       /* A hash full of callbacks, one for each column */
    void *wrapperContext,		       /* Gets passed to callbacks in tagOutWrappers */
    boolean withFilters,	/* If TRUE put up filter controls under labels */
    char *pluralInstructions,   /* If non-NULL put up instructions and clear/search buttons */
    int pageSize,		/* How many items per page */
    int facetUsualSize,		/* How many items in a facet before opening */
    struct hash *suggestHash,	/* If using filter can put suggestions for categorical items here */
    char *visibleFacetList,     /* Comma separated list of fields to facet on */
    void (*addFunc)(int) )      /* Callback relevant with pluralInstructions only */
/* Turn sql query into a nice interactive table, possibly with facets.  It constructs
 * a query to the database in conn that is basically a select query broken into
 * separate clauses, construct and display an HTML table around results. Optionally table
 * may have a faceted search to the left or fields that can filter under the labels.  The table 
 * has column names that will sort the table, and optionally (if withFilters is set)
 * it will also allow field-by-field wildcard queries on a set of controls it draws above
 * the labels. 
 *    Much of the functionality rests on the call to webFilteredFieldedTable.  This function
 * does the work needed to bring in sections of potentially huge results sets into
 * the fieldedTable. */
{
struct dyString *query;
struct dyString *where;
struct dyString *fusedFields = fuseCsvFields(conn, from, fields, visibleFacetList);
webTableBuildQuery(cart, from, initialWhere, varPrefix, 
    fusedFields->string, withFilters, &query, &where);

char selListVar[256];
safef(selListVar, sizeof(selListVar), "%s_facet_selList", varPrefix);
char *selectedFacetValues=cartUsualString(cart, selListVar, "");

struct facetField **ffArray = NULL;
struct fieldedTable *table = NULL;

char pageVar[256];
safef(pageVar, sizeof(pageVar), "%s_page", varPrefix);
int page = 0;
struct fieldedTableSegment context;
page = cartUsualInt(cart, pageVar, 0) - 1;
if (page < 0)
    page = 0;
context.tableOffset = page * pageSize;

if (visibleFacetList)
    {
    table = fieldedTableAndCountsFromDbQuery(conn, query->string, pageSize, 
	context.tableOffset, selectedFacetValues, &ffArray, &context.tableSize);
    }
else
    {
    /* Figure out size of query result */
    sqlCkIl(fromSafe,from)
    struct dyString *countQuery = sqlDyStringCreate("select count(*) from %-s", fromSafe);
    if (!isEmpty(where->string))
	sqlDyStringPrintf(countQuery, "%-s", where->string);   // trust
    context.tableSize = sqlQuickNum(conn, countQuery->string);
    dyStringFree(&countQuery);
    }

if (context.tableSize > pageSize)
    {
    int lastPage = (context.tableSize-1)/pageSize;
    if (page > lastPage)
        page = lastPage;
    context.tableOffset = page * pageSize;
    }

if (!visibleFacetList)
    {
    sqlDyStringPrintf(query, " limit %d offset %d", pageSize, context.tableOffset);
    table = fieldedTableFromDbQuery(conn, query->string);
    }

webFilteredFieldedTable(cart, table, fields, returnUrl, varPrefix, maxFieldWidth, 
    tagOutWrappers, wrapperContext, withFilters, pluralInstructions, 
    pageSize, facetUsualSize, &context, suggestHash, ffArray, visibleFacetList, addFunc, FALSE);
fieldedTableFree(&table);

dyStringFree(&fusedFields);
dyStringFree(&query);
dyStringFree(&where);
}

