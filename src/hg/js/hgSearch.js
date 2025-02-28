var db; /* A global variable for the current assembly. Needed when we may not have
           sent a cartJson request yet */
var hgSearch = (function() {

    // this object contains everything needed to build current state of the page
    var uiState = {
        db: "",              /* The assembly for which all this business belongs to */
        categs: {},          /* all possible categories for this database, this includes all possible
                              * searchable tracks */
        currentCategs: {},   /* the categories (filters) for the current search results */
        positionMatches: [], /* an array of search result objects, one for each category,
                              * created by hgPositionsJson in cartJson.c */
        search: "",          /* what is currently in the search box */
        trackGroups: {},     /* the track groups available for this assembly */
        resultHash: {},      /* positionMatches but each objects' category name is a key */
        genomes: {},         /* Hash of organism name: [{assembly 1}, {assembly 2}, ...] */
        };

    // if true, log to the console anything that comes back from the server (for debug)
    var debugCartJson = false;

    // This object is the parent for all tracks currently hidden on hgTracks
    var hiddenTrackGroup = {
        id: "Currently Hidden Tracks",
        name: "Currently Hidden Tracks",
        label: "Currently Hidden Tracks",
        text: "Currently Hidden Tracks",
        numMatches: 0,
        searchTime: -1,
        children: [],
        state: {opened: true, loaded: true},
        li_attr: {title: "Search for track items in currently hidden tracks"}
    };

    // This object that is the parent for all tracks currently visible on hgTracks
    var visibleTrackGroup = {
        id: "Visible Tracks",
        name: "Visible Tracks",
        label: "Visible Tracks",
        text: "Visible Tracks",
        numMatches: 0,
        searchTime: -1,
        li_attr: {title: "Search for track items in all the currently visible searchable tracks"},
        children: [],
        state: {opened: true, loaded: true},
        priority: 0.0
    };

    // variables to parse url arguments correctly
    var digitTest = /^\d+$/,
        keyBreaker = /([^\[\]]+)|(\[\])/g,
        plus = /\+/g,
        paramTest = /([^?#]*)(#.*)?$/;

    function deparam(params) {
        /* From https://github.com/jupiterjs/jquerymx/blob/master/lang/string/deparam/deparam.js
         * Used by the cell browser */
        if(! params || ! paramTest.test(params) ) {
            return {};
        }

        var data = {},
            pairs = params.split('&'),
            current;

        for (var i=0; i < pairs.length; i++){
            current = data;
            var pair = pairs[i].split('=');

            // if we find foo=1+1=2
            if(pair.length !== 2) {
                pair = [pair[0], pair.slice(1).join("=")];
            }

            var key = decodeURIComponent(pair[0].replace(plus, " ")),
            value = decodeURIComponent(pair[1].replace(plus, " ")),
            parts = key.match(keyBreaker);

            for ( var j = 0; j < parts.length - 1; j++ ) {
                var part = parts[j];
                if (!current[part] ) {
                    //if what we are pointing to looks like an array
                    current[part] = digitTest.test(parts[j+1]) || parts[j+1] === "[]" ? [] : {};
                }
                current = current[part];
                }
            var lastPart = parts[parts.length - 1];
            if (lastPart === "[]"){
                current.push(value);
            } else{
                current[lastPart] = value;
            }
        }
        return data;
    }

    function changeUrl(vars, oldVars) {
        /* Save the users search string to the url so web browser can easily
         * cache search results into the browser history
         * vars: object of new key: val pairs like CGI arguments
         * oldVars: arguments we want to keep between calls */
        var myUrl = window.location.href;
        myUrl = myUrl.replace('#','');
        var urlParts = myUrl.split("?");
        var baseUrl;
        if (urlParts.length > 1)
            baseUrl = urlParts[0];
        else
            baseUrl = myUrl;

        var urlVars;
        if (oldVars === undefined) {
            var queryStr = urlParts[1];
            urlVars = deparam(queryStr);
        } else {
            urlVars = oldVars;
        }

        for (var key in vars) {
            var val = vars[key];
            if (val === null || val === "") {
                if (key in urlVars) {
                    delete urlVars[key];
                }
            } else {
                urlVars[key] = val;
            }
        }

        var argStr = jQuery.param(urlVars);
        argStr = argStr.replace(/%20/g, "+");

        return {"baseUrl": baseUrl, "args": argStr, "urlVars": urlVars};
    }

    function saveHistory(obj, urlParts, replace) {
        /* Save an object to the web browser's history stack. When we visit the page for the
         * first time, we replace the basic state after we are done making the initial UI */
        if (replace) {
            history.replaceState(obj, "", urlParts.baseUrl + (urlParts.args.length !== 0 ? "?" + urlParts.args : ""));
        } else {
            history.pushState(obj, "", urlParts.baseUrl + (urlParts.args.length !== 0 ? "?" + urlParts.args : ""));
        }
    }

    function compareTrack(trackA, trackB) {
        /* comparator function for sorting tracks, lowest priority wins,
         * followed by short label */
        priorityA = trackA.priority;
        priorityB = trackB.priority;

        // if both priorities are undefined or equal to each other, sort
        // on shortlabel alphabetically
        if (priorityA === priorityB) {
            if (trackA.name < trackB.name) {
                return -1;
            } else if (trackA.name > trackB.name) {
                return 1;
            } else {
                return 0;
            }
        } else {
            if (priorityA === undefined) {
                return 1;
            } else if (priorityB === undefined) {
                return -1;
            } else if (priorityA < priorityB) {
                return -1;
            } else if (priorityA > priorityA) {
                return 1;
            } else {
                return 0;
            }
        }
    }

    function compareGroups(a, b) {
        /* Compare function for track group sorting */
        return uiState.trackGroups[a.name].priority - uiState.trackGroups[b.name].priority;
    }

    function sortByTrackGroups(groupList) {
        return groupList.sort(compareGroups);
    }

    function sortTrackCategories(trackList) {
        /* Sort the nested track list structure such that within each group
         * the leaves of the tree are sorted by priority */
        if (trackList.children !== undefined) {
            trackList.children.sort(compareTrack);
            for (var i = 0; i < trackList.children.length; i++) {
                trackList.children[i] = sortTrackCategories(trackList.children[i]);
            }
        }
        return trackList;
    }

    function categoryComp(category) {
        if (category.priority !== undefined)
            return category.priority;
        return 1000.0;
    }

    function sortCategories(categList) {
        return _.sortBy(categList, categoryComp);
    }

    function addCountAndTimeToLabel(categ) {
        /* Change the text label of the node */
        categ.text = categ.label + " (<span id='" + categ.id + "count'><b>" + categ.numMatches + " results</b></span>";
        if (categ.searchTime !== undefined && categ.searchTime >= 0) {
            categ.text += ", <span id='" + categ.id + "searchTime'><b>" + categ.searchTime + "ms searchTime</b></span>";
        }
        categ.text += ")";
    }

    function tracksToTree(trackList) {
        /* Go through the list of all tracks for this assembly, filling
         * out the necessary information for jstree to be able to work.
         * Only include categories that have results.
         * The groups object will get filled out along the way. */
        trackGroups = uiState.trackGroups;
        var ret = [];
        var parentsHash = {};
        var groups = {};
        visibleTrackGroup.children = [];
        visibleTrackGroup.numMatches = 0;
        visibleTrackGroup.searchTime = -1;
        hiddenTrackGroup.children = [];
        hiddenTrackGroup.numMatches = 0;
        hiddenTrackGroup.searchTime = -1;
        _.each(trackList, function(track) {
            if (!(track.id in uiState.resultHash)) {
                return;
            }
            var newCateg = {};
            _.assign(newCateg, track);
            newCateg.text = track.longLabel;
            newCateg.text = track.label;
            var group = track.group;
            newCateg.state = {checked: true, opened: true};
            newCateg.text = track.label;
            newCateg.li_attr = {title: track.description};
            newCateg.numMatches = uiState.resultHash[newCateg.id].matches.length;
            newCateg.searchTime = uiState.resultHash[newCateg.id].searchTime;
            addCountAndTimeToLabel(newCateg);
            if (track.visibility > 0) {
                if (!groups.visible) {
                    groups.visible = visibleTrackGroup;
                }
                groups.visible.children.push(newCateg);
                if (newCateg.searchTime !== undefined) {
                    if (groups.visible.searchTime < 0)
                        groups.visible.searchTime = 0;
                    groups.visible.searchTime += newCateg.searchTime;
                }
                groups.visible.numMatches += newCateg.numMatches;
            } else {
                var last = newCateg;
                var doNewComp = true;
                if (track.parents) {
                    var tracksAndLabels = track.parents.split(',');
                    var l = tracksAndLabels.length;
                    for (var i = 0; i < l; i+=2) {
                        var parentTrack= tracksAndLabels[i];
                        var parentLabel = tracksAndLabels[i+1];
                        if (!(parentTrack in parentsHash)) {
                            parent = {};
                            parent.id = parentTrack;
                            parent.label = parentLabel;
                            parent.children = [last];
                            parent.li_attr = {title: "Search for track items in all of the searchable subtracks of the " + parentLabel + " track"};
                            parent.numMatches = last.numMatches;
                            parent.searchTime = last.searchTime;
                            parentsHash[parentTrack] = parent;
                            addCountAndTimeToLabel(parent);
                            last = parent;
                            doNewComp = true;
                        } else if (last !== undefined) {
                            // if we are processing the first parent, we need to add ourself (last)
                            // as a child so the subtrack list is correct, but we still need
                            // to go up through the parent list and update the summarized counts
                            if (doNewComp) {
                                parentsHash[parentTrack].children.push(last);
                            }
                            doNewComp = false;
                            parentsHash[parentTrack].numMatches += last.numMatches;
                            if (last.searchTime !== undefined) {
                                parentsHash[parentTrack].searchTime += last.searchTime;
                            }
                            addCountAndTimeToLabel(parent);
                        }
                    }
                }
                if (groups[group] !== undefined && last !== undefined) {
                    groups[group].numMatches += last.numMatches;
                    if (last.searchTime !== undefined) {
                        groups[group].searchTime += last.searchTime;
                    }
                    addCountAndTimeToLabel(groups[group]);
                    if (doNewComp) {
                        groups[group].children.push(last);
                    }
                } else if (doNewComp) {
                    groups[group] = {};
                    groups[group].id = group;
                    groups[group].name = group;
                    groups[group].label = group;
                    addCountAndTimeToLabel(groups[group]);
                    groups[group].numMatches = last.numMatches;
                    groups[group].searchTime = last.searchTime;
                    groups[group].children = [last];
                    if (trackGroups !== undefined && group in trackGroups) {
                        groups[group].priority = trackGroups[group].priority;
                        groups[group].label = trackGroups[group].label;
                        addCountAndTimeToLabel(groups[group]);
                    } else {
                        trackGroups[group] = groups[group];
                    }
                }
            }
        });
        if ("visible" in groups) {
            groups.visible.children = sortTrackCategories(groups.visible.children);
            addCountAndTimeToLabel(groups.visible);
            ret.push(groups.visible);
        }
        hiddenTrackChildren = [];
        _.each(groups, function(group) {
            if (group.id !== "Visible Tracks") {
                group.li_attr = {title: "Search for track items in the " + group.label+ " set of tracks"};
                group.state = {checked: true, opened: true};
                group.children = sortTrackCategories(group.children);
                hiddenTrackChildren.push(group);
            }
        });
        if (hiddenTrackChildren.length > 0) {
            hiddenTrackChildren = sortByTrackGroups(hiddenTrackChildren);
            _.each(hiddenTrackChildren, function(group) {
                hiddenTrackGroup.children.push(group);
                if (group.searchTime !== undefined) {
                    if (hiddenTrackGroup.searchTime < 0) {
                        hiddenTrackGroup.searchTime = 0;
                    }
                    hiddenTrackGroup.searchTime += group.searchTime;
                }
                hiddenTrackGroup.numMatches += group.numMatches;
            });
            addCountAndTimeToLabel(hiddenTrackGroup);
            ret.push(hiddenTrackGroup);
        }
        return ret;
    }

    function filtersToJstree() {
        /* Turns uiState.categs into uiState.currentCategs, which populates the
         * tree of filters. We only make a leaf node in the tree if that leaf
         * has a search result */
        thisCategs = {};
        _.each(uiState.categs, function(categ) {
            var newCateg = {};
            if (categ.id === "trackData") {
                // this id will never be in the results since we only get a result
                // per leaf node, so handle this case separately
                _.assign(newCateg, categ);
                newCateg.text = categ.label;
                newCateg.li_attr = {title: newCateg.description};
                newCateg.state = {opened: true, loaded: true, checked: true};
                newCateg.children = tracksToTree(categ.tracks);
                newCateg.searchTime = 0;
                newCateg.numMatches = 0;
                if (_.isEmpty(newCateg.children)) {
                    return true; // goes to next instance of _.each()
                }
                _.each(newCateg.children, function(track) {
                    newCateg.searchTime += track.searchTime;
                    newCateg.numMatches += track.numMatches;
                });
                addCountAndTimeToLabel(newCateg);
            } else if (categ.id in uiState.resultHash) {
                _.assign(newCateg, categ);
                newCateg.numMatches = uiState.resultHash[categ.id].matches.length;
                newCateg.searchTime = uiState.resultHash[categ.id].searchTime;
                addCountAndTimeToLabel(newCateg);
                newCateg.li_attr = {title: "Show/hide hits to " + newCateg.description};
                newCateg.state = {opened: true, loaded: true, checked: true};
            }

            if (!_.isEmpty(newCateg))
                thisCategs[categ.id] = newCateg;
        });

        // all of the currentCategs need to be children of the root node
        uiState.currentCategs['#'] = {
            id: '#',
            children: sortCategories(Object.keys(thisCategs).map(function(ele) {
                return thisCategs[ele];
            }))
        };
    }

    function showOrHideResults(event, node) {
        /* When a checkbox is checked/uncheck in the tree, show/hide the corresponding
         * result section in the list of results */
        var state = node.state.checked;
        if (node.children.length > 0) {
            _.each(node.children, function(n) {
                showOrHideResults(event, $("#searchCategories").jstree().get_node(n));
            });
        } else {
            resultLi = $('[id="' + node.id + 'Results"');
            if (resultLi !== undefined) // if we don't have any results for this track resultLi is undefined
                _.each(resultLi, function(li) {
                    li.style = state ? "display" : "display: none";
                });
        }
    }

    function buildTree(node, cb) {
        cb.call(this, uiState.currentCategs[node.id]);
    }

    function makeCategoryTree() {
        var parentDiv = $("#searchCategories");
        $.jstree.defaults.core.themes.icons = false;
        $.jstree.defaults.core.themes.dots = true;
        $.jstree.defaults.contextmenu.show_at_node = false;
        parentDiv.jstree({
            'plugins' : ['contextmenu', 'checkbox'],
            'core': {
                'data': buildTree,
                'check_callback': true
            },
            'checkbox': {
                'tie_selection': false
            }
        });
        parentDiv.css('height', "auto");
    }

    function updateFilters(uiState) {
        if (uiState.categs !== undefined) {
            filtersToJstree();
            makeCategoryTree();
        }
    }

    function clearOldFacetCounts() {
        $("[id*='extraInfo']").remove();
    }

    function printMatches(list, matches, title, searchDesc) {
        var printCount = 0;
        _.each(matches, function(match, printCount) {
            var position = match.position.split(':');
            var url, matchTitle;
            if (title === "helpDocs") {
                url = position[0];
                matchTitle = "<b>" + position[1].replace(/_/g, " ") + "</b>";
            } else if (title === "publicHubs") {
                var hubUrl = position[0] + ":" + position[1];
                var dbName = position[2];
                var track = position[3];
                var hubShortLabel = position[4];
                var hubLongLabel = position[5];
                url = "hgTrackUi?hubUrl=" + hubUrl + "&g=" + track + "&db=" + dbName;
                matchTitle = "<b>" + hubShortLabel + "</b>";
            } else if (title === "trackDb") {
                var trackName = position[0];
                var shortLabel = position[1];
                var longLabel = position[2];
                url = "hgTrackUi?g=" + trackName;
                matchTitle = "<b>" +  shortLabel + " - " + longLabel + "</b>";
            } else {
                // unaligned mrnas and ests can still be searched but all you can get
                // to is the hgc page, no hgTracks for them
                goToHgTracks = true;
                hgTracksTitle = hgcTitle = title;
                if (["all_mrna", "all_est", "xenoMrna", "xenoEst", "intronEst"].includes(title)) {
                    hgTracksTitle = title.replace(/all_/, "");
                    if (searchDesc.includes("Unaligned"))
                        goToHgTracks = false;
                }
                if (goToHgTracks) {
                    url = "hgTracks?db=" + db + "&" + hgTracksTitle + "=pack&position=" + match.position + "&hgFind.matches=" + match.hgFindMatches;
                    if (match.extraSel)
                        url += "&" + match.extraSel;
                    if (match.highlight) {
                        url += url[url.length-1] !== '&' ? '&' : '';
                        url += "highlight=" + match.highlight;
                    }
                } else {
                    url = "hgc?db=" + db + "&g=" + hgcTitle + "&i=" + match.position + "&c=0&o=0&l=0&r=0" ;
                }
                matchTitle = match.posName;
                //if (match.canonical === true)
                matchTitle = "<b>" + matchTitle + "</b>";
            }
            var newListObj;
            if (printCount < 500) {
                if (printCount + 1 > 10) {
                    newListObj = "<li class='" + title + "_hidden' style='display: none'><a href=\"" + url + "\">" + matchTitle + "</a> - ";
                } else {
                    newListObj = "<li><a href=\"" + url + "\">" + matchTitle + "</a> - ";
                }
                printedPos = false;
                if (!(["helpDocs", "publicHubs", "trackDb"].includes(title))) {
                    newListObj += match.position;
                    printedPos = true;
                }
                if (match.description) {
                    if (printedPos) {newListObj += " - ";}
                    newListObj += match.description;
                }
                newListObj += "</li>";
                list.innerHTML += newListObj;
                printCount += 1;
            }
        });
    }

    function showMoreResults() {
        var trackName = this.id.replace(/Results_.*/, "");
        var isHidden = $("." + trackName + "_hidden")[0].style.display === "none";
        _.each($("." + trackName + "_hidden"), function(hiddenLi) {
            if (isHidden) {
                hiddenLi.style = "display:";
            } else {
                hiddenLi.style = "display: none";
            }
        });
        if (isHidden) {
            newText = this.nextSibling.innerHTML.replace(/Show/,"Hide");
            this.nextSibling.innerHTML = newText;
            this.src = "../images/remove_sm.gif";
        } else {
            newText = this.nextSibling.innerHTML.replace(/Hide/,"Show");
            this.nextSibling.innerHTML = newText;
            this.src = "../images/add_sm.gif";
        }
    }

    function collapseNode() {
        var toCollapse = this.parentNode.childNodes[3];
        var isHidden  = toCollapse.style.display === "none";
        if (isHidden)
            {
            toCollapse.style = 'display:';
            this.src = "../images/remove_sm.gif";
            }
        else
            {
            toCollapse.style = 'display: none';
            this.src = "../images/add_sm.gif";
            }
    }

    function updateSearchResults(uiState) {
        var parentDiv = $("#searchResults");
        if (uiState && uiState.search !== undefined) {
            $("#searchBarSearchString").val(uiState.search);
        } else {
            // back button all the way to the beginning
            $("#searchBarSearchString").val("");
        }
        if (uiState && uiState.positionMatches && uiState.positionMatches.length > 0) {
            // clear the old search results if there were any:
            parentDiv.empty();

            // create the elements that will hold results:
            var newList = document.createElement("ul");
            var noUlStyle = document.createAttribute("class");
            noUlStyle.value = "ulNoStyle";
            newList.setAttributeNode(noUlStyle);
            parentDiv.append(newList);

            clearOldFacetCounts();
            var categoryCount = 0;
            // Loop through categories of match (public hubs, help docs, a single track, ...
            _.each(uiState.positionMatches, function(categ) {
                var title = categ.name;
                var searchDesc = categ.description;
                var matches = categ.matches;
                var numMatches = matches.length;
                var newListObj = document.createElement("li");
                var idAttr = document.createAttribute("id");
                idAttr.value = title + 'Results';
                newListObj.setAttributeNode(idAttr);
                var noLiStyle = document.createAttribute("class");
                noLiStyle.value = "liNoStyle";
                newListObj.setAttributeNode(noLiStyle);
                newListObj.innerHTML += "<input type='hidden' id='" + idAttr.value + categoryCount + "' value='0'>";
                newListObj.innerHTML += "<img height='18' width='18' id='" + idAttr.value + categoryCount + "_button' src='../images/remove_sm.gif'>";
                newListObj.innerHTML += "&nbsp;" + searchDesc + ":";
                //printOneFullMatch(newList, matches[0], title, searchDesc);
                // Now loop through each actual hit on this table and unpack onto list
                var subList = document.createElement("ul");
                printMatches(subList, matches, title, searchDesc);
                if (matches.length > 10) {
                    subList.innerHTML += "<li class='liNoStyle'>";
                    subList.innerHTML += "<input type='hidden' id='" + idAttr.value + "_" + categoryCount +  "showMore' value='0'>";
                    subList.innerHTML += "<img height='18' width='18' id='" + idAttr.value + "_" + categoryCount + "_showMoreButton' src='../images/add_sm.gif'>";
                    if (matches.length > 500)
                        subList.innerHTML += "<div class='showMoreDiv' id='" + idAttr.value+"_"+categoryCount+"_showMoreDiv'>&nbsp;Show 490 (out of " + (matches.length) + " total) more matches for " + searchDesc + "</div></li>";
                    else
                        subList.innerHTML += "<div class='showMoreDiv' id='" + idAttr.value+"_"+categoryCount+"_showMoreDiv'>&nbsp;Show " + (matches.length - 10) + " more matches for " + searchDesc + "</div></li>";
                }
                newListObj.append(subList);
                newList.append(newListObj);

                // make result list collapsible:
                $('#'+idAttr.value+categoryCount+"_button").click(collapseNode);
                $('#'+idAttr.value+"_" +categoryCount+"_showMoreButton").click(showMoreResults);
                categoryCount += 1;
            });
        } else if (uiState && uiState.search !== undefined) {
            // No results from match
            var msg = "<p>No results for: <b>" + uiState.search + "<b></p>";
            parentDiv.empty();
            parentDiv.html(msg);
            clearOldFacetCounts();
        } else {
            parentDiv.empty();
        }
    }

    function fillOutAssemblies(e) {
        organism = $("#speciesSelect")[0].value;
        select = $("#dbSelect");
        select.empty();
        _.each(_.sortBy(uiState.genomes[organism], ['orderKey']), function(assembly) {
            newOpt = document.createElement("option");
            newOpt.value = assembly.name;
            newOpt.label = trackHubSkipHubName(assembly.organism) + " " + assembly.description;
            if (assembly.name == db) {
                newOpt.selected = true;
            }
            $("#dbSelect").append(newOpt);
        });
        // if we are getting here from a change event on the species dropdown
        // and are switching to the default assembly for a species, we can
        // automatically send a search for this organism+assembly
        if (e !== undefined) {
            switchAssemblies($("#dbSelect")[0].value);
        }
    }

    function buildSpeciesDropdown() {
        // Process the species select dropdowns
        _.each(uiState.genomes, function(genome) {
            newOpt = document.createElement("option");
            newOpt.value = genome[0].organism;
            newOpt.label = trackHubSkipHubName(genome[0].organism);
            if (genome.some(function(assembly) {
                if (assembly.isCurated) {
                    if (assembly.name === trackHubSkipHubName(db)) {
                        return true;
                    }
                } else {
                    if (assembly.name === db) {
                        return true;
                    }
                }
            })) {
                newOpt.selected = true;
            }
            $("#speciesSelect").append(newOpt);
        });
    }

    function changeSearchResultsLabel() {
        // change the title to indicate what assembly was search:
        $("#dbPlaceholder").empty();
        $("#dbPlaceholder").append("on " + db + " (" + $("#dbSelect")[0].selectedOptions[0].label+ ")");
    }

    function checkJsonData(jsonData, callerName) {
        // Return true if jsonData isn't empty and doesn't contain an error;
        // otherwise complain on behalf of caller.
        if (! jsonData) {
            alert(callerName + ': empty response from server');
        } else if (jsonData.error) {
            console.error(jsonData.error);
            alert(callerName + ': error from server: ' + jsonData.error);
        } else {
            if (debugCartJson) {
                console.log('from server:\n', jsonData);
            }
            return true;
        }
        return false;
    }

    function updateStateAndPage(jsonData, doSaveHistory) {
        // Update uiState with new values and update the page.
        _.assign(uiState, jsonData);
        db = uiState.db;
        if (jsonData.positionMatches !== undefined) {
            // clear the old resultHash
            uiState.resultHash = {};
            _.each(uiState.positionMatches, function(match) {
                uiState.resultHash[match.name] = match;
            });
        } else {
            // no results for this search
            uiState.resultHash = {};
            uiState.positionMatches = [];
        }
        updateFilters(uiState);
        updateSearchResults(uiState);
        buildSpeciesDropdown();
        fillOutAssemblies();
        urlVars = {"db": db, "search": uiState.search, "showSearchResults": ""};
        // changing the url allows the history to be associated to a specific url
        var urlParts = changeUrl(urlVars);
        $("#searchCategories").jstree(true).refresh(false,true);
        if (doSaveHistory)
            saveHistory(uiState, urlParts);
        changeSearchResultsLabel();
    }

    function handleRefreshState(jsonData) {
        if (checkJsonData(jsonData, 'handleRefreshState')) {
            updateStateAndPage(jsonData, true);
        }
        $("#spinner").remove();
    }

    function handleErrorState(jqXHR, textStatus) {
        cart.defaultErrorCallback(jqXHR, textStatus);
        $("#spinner").remove();
    }

    function sendUserSearch() {
        // User has clicked the search button, if they also entered a search
        // term, fire off a search
        cart.debug(debugCartJson);
        var searchTerm = $("#searchBarSearchString").val().replaceAll("\"","");
        if (searchTerm !== undefined && searchTerm.length > 0) {
            // put up a loading image
            $("#searchBarSearchButton").after("<i id='spinner' class='fa fa-spinner fa-spin'></i>");

            // if the user entered a plain position string like chr1:blah-blah, just
            // go to the old cgi/hgTracks
            var canonMatch = searchTerm.match(canonicalRangeExp);
            var gbrowserMatch = searchTerm.match(gbrowserRangeExp);
            var lengthMatch = searchTerm.match(lengthRangeExp);
            var bedMatch = searchTerm.match(bedRangeExp);
            var sqlMatch = searchTerm.match(sqlRangeExp);
            var singleMatch = searchTerm.match(singleBaseExp);
            var positionMatch = canonMatch || gbrowserMatch || lengthMatch || bedMatch || sqlMatch || singleMatch;
            if (positionMatch !== null) {
                var prevCgi = uiState.prevCgi !== undefined ? uiState.prevCgi : "hgTracks";
                window.location.replace("../cgi-bin/" + prevCgi + "?db=" + db + "position=" + searchTerm);
                return;
            }

            _.assign(uiState, {"search": searchTerm});
            cart.send({ getSearchResults:
                        {
                        db: db,
                        search: searchTerm
                        }
                    },
                    handleRefreshState,
                    handleErrorState);
            // always update the results when a search has happened
            cart.flush();
        }
    }

    function switchAssemblies(newDb) {
        // reload the page to attach curated hub (if any)
        re = /db=[\w,\.]*/;
        window.location = window.location.href.replace(re,"db="+newDb);
    }

    function init() {
        cart.setCgi('hgSearch');
        cart.debug(debugCartJson);
        // If a user clicks search before the page has finished loading
        // start processing it now:
        $("#searchBarSearchButton").click(sendUserSearch);
        if (typeof cartJson !== "undefined") {
            if (cartJson.db !== undefined) {
                db = cartJson.db;
            } else {
                alert("Error no database from request");
            }
            var urlParts = {};
            if (debugCartJson) {
                console.log('from server:\n', cartJson);
            }
            if (typeof cartJson.search !== "undefined") {
                urlParts = changeUrl({"search": cartJson.search});
            } else {
                urlParts = changeUrl({"db": db});
                cartJson.search = urlParts.urlVars.search;
            }
            _.assign(uiState,cartJson);
            if (typeof cartJson.categs  !== "undefined") {
                _.each(uiState.positionMatches, function(match) {
                    uiState.resultHash[match.name] = match;
                });
                filtersToJstree();
                makeCategoryTree();
            } else {
                cart.send({ getUiState: {db: db} }, handleRefreshState);
                cart.flush();
            }
            $("#searchCategories").bind('ready.jstree', function(e, data) {
                // wait for the category jstree to finish loading before showing the results
                $("#searchBarSearchString").val(uiState.search);
                updateSearchResults(uiState);

                // when a category is checked/unchecked we show/hide that result
                // from the result list
                $("#searchCategories").on('check_node.jstree uncheck_node.jstree', function(e, data) {
                    if ($("#searchResults")[0].children.length > 0) {
                        showOrHideResults(e,data.node);
                    }
                });
            });
            saveHistory(cartJson, urlParts, true);
        } else {
            // no cartJson object means we are coming to the page for the first time:
            cart.send({ getUiState: {db: db} }, handleRefreshState);
            cart.flush();
        }

        buildSpeciesDropdown(); // make the list of available organisms
        fillOutAssemblies(); // call once to get the initial state
        $("#speciesSelect").change(fillOutAssemblies);
        $("#dbSelect").change(function(e) {
            e.preventDefault();
            db = e.currentTarget.value;
            switchAssemblies(db);
        });
        changeSearchResultsLabel();
    }

    return { init: init,
             updateSearchResults: updateSearchResults,
             updateFilters: updateFilters,
             updateStateAndPage: updateStateAndPage
           };

}());

$(document).ready(function() {
    $('#searchBarSearchString').bind('keypress', function(e) {  // binds listener to search button
        if (e.which === 13) {  // listens for return key
            e.preventDefault();   // prevents return from also submitting whole form
            if ($("#searchBarSearchString").val() !== undefined) {
                $('#searchBarSearchButton').focus().click(); // clicks search button button
            }
        }
    });
});

// when a user reaches this page from the back button we can display our saved state
// instead of sending another network request
window.onpopstate = function(event) {
    event.preventDefault();
    hgSearch.updateStateAndPage(event.state, false);
};
