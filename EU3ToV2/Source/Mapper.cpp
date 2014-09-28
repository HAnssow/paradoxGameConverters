#include "Mapper.h"
#include "Log.h"
#include "Configuration.h"
#include "Parsers\Object.h"
#include "EU3World/EU3World.h"
#include "EU3World/EU3Country.h"
#include "EU3World/EU3Province.h"
#include "V2World/V2World.h"
#include "V2World/V2Country.h"
#include <algorithm>
#include <sys/stat.h>



void initProvinceMap(Object* obj, WorldType worldType, provinceMapping& provinceMap, inverseProvinceMapping& inverseProvinceMap, resettableMap& resettableProvinces)
{
	provinceMapping::iterator mapIter;

	vector<Object*> worldTypeLeaves = obj->getLeaves();

	if (worldTypeLeaves.size() < 1)
	{
		log ("\tError: No province mapping definitions loaded.\n");
		printf("\tError: No province mapping definitions loaded.\n");
		return;
	}

	unsigned int mappingIdx = -1;
	for (unsigned int i = 0; i < worldTypeLeaves.size(); i++)
	{
		switch(worldType)
		{
			case InNomine:
			{
				if (worldTypeLeaves[i]->getKey() == "in")
				{
					mappingIdx = i;
				}
				break;
			}
			case HeirToTheThrone:
			{
				if (worldTypeLeaves[i]->getKey() == "httt")
				{
					mappingIdx = i;
				}
				break;
			}
			case DivineWind:
			{
				if (worldTypeLeaves[i]->getKey() == "dw")
				{
					mappingIdx = i;
				}
				break;
			}
			case unknown:
			case VeryOld:
			default:
			{
				log("Error: Unsupported world type. Cannot map provinces!\n");
				printf("Error: Unsupported world type. Cannot map provinces!\n");
				exit(-1);
			}
		}
	}

	log("Using version %s mappings\n", worldTypeLeaves[mappingIdx]->getKey().c_str());
	vector<Object*> data = worldTypeLeaves[mappingIdx]->getLeaves();

	for (vector<Object*>::iterator i = data.begin(); i != data.end(); i++)
	{
		vector<int> EU3nums;
		vector<int> V2nums;
		bool			resettable = false;

		vector<Object*> euMaps = (*i)->getLeaves();

		for (vector<Object*>::iterator j = euMaps.begin(); j != euMaps.end(); j++)
		{
			if ( (*j)->getKey() == "eu3" )
			{
				EU3nums.push_back(  atoi( (*j)->getLeaf().c_str() )  );
			}
			else if ( (*j)->getKey() == "vic" )
			{
				V2nums.push_back(  atoi( (*j)->getLeaf().c_str() )  );
			}
			else if ((*j)->getKey() == "resettable")
			{
				resettable = true;
			}
			else
			{
				log("\tWarning: unknown data while mapping provinces.\n");
			}
		}

		if (EU3nums.size() == 0)
		{
			EU3nums.push_back(0);
		}
		if (V2nums.size() == 0)
		{
			V2nums.push_back(0);
		}

		for (vector<int>::iterator j = V2nums.begin(); j != V2nums.end(); j++)
		{
			if (*j != 0)
			{
				provinceMap.insert(make_pair(*j, EU3nums));
			}
			if (resettable)
			{
				resettableProvinces.insert(*j);
			}
		}
		for (vector<int>::iterator j = EU3nums.begin(); j != EU3nums.end(); j++)
		{
			if (*j != 0)
			{
				inverseProvinceMap.insert(make_pair(*j, V2nums));
			}
		}
	}
}


static const vector<int> empty_vec;
const vector<int>& getV2ProvinceNums(const inverseProvinceMapping& invProvMap, int eu3ProvinceNum)
{
	inverseProvinceMapping::const_iterator itr = invProvMap.find(eu3ProvinceNum);
	if (itr == invProvMap.end())
	{
		return empty_vec;
	}
	else
	{
		return itr->second;
	}
}


adjacencyMapping initAdjacencyMap()
{
	FILE* adjacenciesBin = NULL;
	string filename = Configuration::getV2DocumentsPath() + "\\map\\cache\\adjacencies.bin";
	struct _stat st;
	if ((_stat(filename.c_str(), &st) != 0))
	{
		filename = Configuration::getV2Path() + "\\map\\cache\\adjacencies.bin";;
	}
	fopen_s(&adjacenciesBin, filename.c_str(), "rb");
	if (adjacenciesBin == NULL)
	{
		log("Error: Could not open %s\n", filename.c_str());
		exit(1);
	}

	adjacencyMapping adjacencyMap;
	while (!feof(adjacenciesBin))
	{
		int numAdjacencies;
		if (fread(&numAdjacencies, sizeof(numAdjacencies), 1, adjacenciesBin) != 1)
		{
			break;
		}
		vector<adjacency> adjacencies;
		for (int i = 0; i < numAdjacencies; i++)
		{
			adjacency newAdjacency;
			fread(&newAdjacency, sizeof(newAdjacency), 1, adjacenciesBin);
			adjacencies.push_back(newAdjacency);
		}
		adjacencyMap.push_back(adjacencies);
	}
	fclose(adjacenciesBin);

	FILE* adjacenciesData;
	fopen_s(&adjacenciesData, "adjacenciesData.csv", "w");
	fprintf(adjacenciesData, "From,Type,To,Via,Unknown1,Unknown2,PathX,PathY\n");
	for (unsigned int from = 0; from < adjacencyMap.size(); from++)
	{
		vector<adjacency> adjacencies = adjacencyMap[from];
		for (unsigned int i = 0; i < adjacencies.size(); i++)
		{
			fprintf(adjacenciesData, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", from, adjacencies[i].type, adjacencies[i].to, adjacencies[i].via, adjacencies[i].unknown1, adjacencies[i].unknown2, adjacencies[i].pathX, adjacencies[i].pathY, adjacencies[i].unknown3, adjacencies[i].unknown4);
		}
	}
	fclose(adjacenciesData);
	
	return adjacencyMap;
}


void initContinentMap(Object* obj, continentMapping& continentMap)
{
	vector<Object*> continentObjs = obj->getLeaves();
	for (unsigned int i = 0; i < continentObjs.size(); i++)
	{
		string continent = continentObjs[i]->getKey();
		vector<string> provinceNums = continentObjs[i]->getTokens();
		for (unsigned int j = 0; j < provinceNums.size(); j++)
		{
			int province = atoi(provinceNums[j].c_str());
			continentMap.insert(make_pair(province, continent));
		}
	}
}


vector<string> processBlockedNations(Object* obj)
{
	vector<string> blockedNations;

	vector<Object*> leaves = obj->getLeaves();
	if (leaves.size() < 1)
	{
		return blockedNations;
	}

	vector<Object*> nations = leaves[0]->getLeaves();
	for (unsigned int i = 0; i < nations.size(); i++)
	{
		blockedNations.push_back(nations[i]->getLeaf());
	}

	return blockedNations;
}


int initCountryMap(countryMapping& mapping, const EU3World& srcWorld, const V2World& destWorld, const vector<string>& blockedNations, Object* rulesObj)
{
	mapping.clear();
	countryMapping::iterator mapIter;

	// get rules
	vector<Object*> leaves = rulesObj->getLeaves();
	if (leaves.size() < 1)
	{
		log ("\tError: No country mapping definitions loaded.\n");
		printf("\tError: No country mapping definitions loaded.\n");
		return -1;
	}
	vector<Object*> rules = leaves[0]->getLeaves();

	map<string, EU3Country*>	EU3Countries		= srcWorld.getCountries();
	map<string, V2Country*>		V2Countries			= destWorld.getPotentialCountries();
	map<string, V2Country*>		dynamicCountries	= destWorld.getDynamicCountries();
	for (vector<Object*>::iterator i = rules.begin(); i != rules.end(); ++i)
	{
		vector<Object*> rule = (*i)->getLeaves();
		string			rEU3Tag;
		vector<string>	rV2Tags;
		for (vector<Object*>::iterator j = rule.begin(); j != rule.end(); j++)
		{
			if ( (*j)->getKey() == "eu3" )
			{		 
				rEU3Tag = (*j)->getLeaf();
			}
			else if ( (*j)->getKey() == "vic" )
			{
				rV2Tags.push_back( (*j)->getLeaf() );
			}
			else
			{
				log("\tWarning: unknown data while mapping countries.\n");
			}
		}

		// find the EU3 country from the rule
		map<string, EU3Country*>::const_iterator EU3Country = EU3Countries.find(rEU3Tag);
		if (EU3Country == EU3Countries.end())
		{
			continue;
		}

		// find the first available V2 tag from the rule
		unsigned int distance = 0;
		map<string, V2Country*>::const_iterator V2Country;// = V2Countries.end();
		for (vector<string>::reverse_iterator j = rV2Tags.rbegin(); j != rV2Tags.rend(); ++j)
		{
			++distance;
			V2Country = V2Countries.find(*j);
			if (V2Country != V2Countries.end())
			{
				//add the mapping
				mapping.insert(make_pair(EU3Country->first, V2Country->first));
				log("\tAdded map %s -> %s (#%d)\n", EU3Country->first.c_str(),V2Country->first.c_str(), distance);

				//remove tags from the lists
				EU3Countries.erase(EU3Country);
				V2Countries.erase(V2Country);
				break;
			}
		}
	}

	map<string, EU3Country*>::iterator itr = EU3Countries.find("REB");
	if ( itr != EU3Countries.end() )
	{
		mapping.insert(make_pair(itr->first, "REB"));
		EU3Countries.erase(itr);
	}
	itr = EU3Countries.find("PIR");
	if ( itr != EU3Countries.end() )
	{
		mapping.insert(make_pair(itr->first, "REB"));
		EU3Countries.erase(itr);
	}
	itr = EU3Countries.find("NAT");
	if ( itr != EU3Countries.end() )
	{
		mapping.insert(make_pair(itr->first, "REB"));
		EU3Countries.erase(itr);
	}

	for (vector<string>::const_iterator i = blockedNations.begin(); i != blockedNations.end(); ++i)
	{
		map<string, V2Country*>::iterator j = V2Countries.find(*i);
		if (j != V2Countries.end())
		{
			V2Countries.erase(j);
		}
	}

	while ( (EU3Countries.size() > 0) && (V2Countries.size() > 0) )
	{
		map<string, V2Country*>::iterator	V2CountryItr = V2Countries.begin();
		map<string, V2Country*>::iterator	dynamicCountry = dynamicCountries.find(V2CountryItr->first);
		if (dynamicCountry == dynamicCountries.end())
		{
			map<string, EU3Country*>::iterator	EU3Country = EU3Countries.begin();
			mapping.insert(make_pair(EU3Country->first, V2CountryItr->first));
			log("\tAdded map %s -> %s (fallback)\n", EU3Country->first.c_str(), V2CountryItr->first.c_str());
			EU3Countries.erase(EU3Country);
		}
		V2Countries.erase(V2CountryItr);
	}

	return EU3Countries.size();
}


void mergeNations(EU3World& world, Object* mergeObj)
{
	vector<Object*> rules = mergeObj->getValue("merge_nations");
	if (rules.size() < 0)
	{
		log("\tNo nations have merging requested (skipping).\n");
		return;
	}

	rules = rules[0]->getLeaves();
	for (vector<Object*>::iterator itr = rules.begin(); itr != rules.end(); ++itr)
	{
		if ((*itr)->getKey() == "merge_daimyos")
		{
			if ((*itr)->getLeaf() == "yes")
			{
				uniteJapan(world);
			}
			continue;
		}

		vector<Object*> thisMerge = (*itr)->getLeaves();
		string masterTag;
		vector<string> slaveTags;
		bool enabled = false;
		for (vector<Object*>::iterator jtr = thisMerge.begin(); jtr != thisMerge.end(); ++jtr)
		{
			if ((*jtr)->getKey() == "merge" && (*jtr)->getLeaf() == "yes")
			{
				enabled = true;
			}
			else if ((*jtr)->getKey() == "master")
			{
				masterTag = (*jtr)->getLeaf();
			}
			else if ((*jtr)->getKey() == "slave")
			{
				slaveTags.push_back((*jtr)->getLeaf());
			}
		}
		EU3Country* master = world.getCountry(masterTag);
		if ( enabled && (master != NULL) && (slaveTags.size() > 0) )
		{
			for (vector<string>::iterator sitr = slaveTags.begin(); sitr != slaveTags.end(); ++sitr)
			{
				master->eatCountry(world.getCountry(*sitr));
			}
		}
	}
}


void uniteJapan(EU3World& world)
{
	EU3Country* japan = world.getCountry("JAP");
	if (japan == NULL)
	{
		return;
	}
	if( japan->hasFlag("united_daimyos_of_japan") )
	{
		return;
	}

	map<string, EU3Country*> countries = world.getCountries();
	for (map<string, EU3Country*>::iterator i = countries.begin(); i != countries.end(); ++i)
	{
		if ( i->second->getPossibleDaimyo() )
		{
			japan->eatCountry(i->second);
		}
	}
}


void removeEmptyNations(EU3World& world)
{
	map<string, EU3Country*> countries = world.getCountries();
	for (map<string, EU3Country*>::iterator i = countries.begin(); i != countries.end(); i++)
	{
		vector<EU3Province*> provinces	= i->second->getProvinces();
		vector<EU3Province*> cores			= i->second->getCores();
		if ( (provinces.size()) == 0 && (cores.size() == 0) )
		{
			world.removeCountry(i->first);
		}
	}
}


void removeDeadLandlessNations(EU3World& world)
{
	map<string, EU3Country*> allCountries = world.getCountries();

	vector<EU3Country*> landlessCountries;
	for (map<string, EU3Country*>::iterator i = allCountries.begin(); i != allCountries.end(); i++)
	{
		vector<EU3Province*> provinces = i->second->getProvinces();
		if (provinces.size() == 0)
		{
			landlessCountries.push_back(i->second);
		}
	}

	for (vector<EU3Country*>::iterator countryItr = landlessCountries.begin(); countryItr != landlessCountries.end(); countryItr++)
	{
		string primaryCulture		= (*countryItr)->getPrimaryCulture();
		vector<EU3Province*> cores	= (*countryItr)->getCores();
		bool cultureSurvives			= false;
		for (vector<EU3Province*>::iterator coreItr = cores.begin(); coreItr != cores.end(); coreItr++)
		{
			if ( (*coreItr)->getOwner() == NULL)
			{
				continue;
			}
			if (  (*coreItr)->getOwner()->getPrimaryCulture() == primaryCulture  )
			{
				continue;
			}

			vector<EU3PopRatio> popRatios = (*coreItr)->getPopRatios();
			double culturePercent = 0.0f;
			for (vector<EU3PopRatio>::iterator popItr = popRatios.begin(); popItr != popRatios.end(); popItr++)
			{
				if (popItr->culture == primaryCulture)
				{
					culturePercent += popItr->popRatio;
				}
			}
			if ( culturePercent >= 0.5 )
			{
				cultureSurvives = true;
				break;
			}
		}

		if (cultureSurvives == false)
		{
			world.removeCountry( (*countryItr)->getTag() );
		}
	}
}


static bool compareLandlessNationsAges(EU3Country* A, EU3Country* B)
{
	vector<EU3Province*> ACores = A->getCores();
	string ATag = A->getTag();
	date ADate;
	for (vector<EU3Province*>::iterator i = ACores.begin(); i != ACores.end(); i++)
	{
		date newADate = (*i)->getLastPossessedDate(ATag);
		if (newADate > ADate)
		{
			ADate = newADate;
		}
	}

	vector<EU3Province*> BCores = B->getCores();
	string BTag = B->getTag();
	date BDate;
	for (vector<EU3Province*>::iterator i = BCores.begin(); i != BCores.end(); i++)
	{
		date newBDate = (*i)->getLastPossessedDate(BTag);
		if (newBDate > BDate)
		{
			BDate = newBDate;
		}
	}

	return (ADate < BDate);
}


void removeOlderLandlessNations(EU3World& world, int excess)
{
	map<string, EU3Country*> allCountries = world.getCountries();

	vector<EU3Country*> landlessCountries;
	for (map<string, EU3Country*>::iterator i = allCountries.begin(); i != allCountries.end(); i++)
	{
		vector<EU3Province*> provinces = i->second->getProvinces();
		if (provinces.size() == 0)
		{
			landlessCountries.push_back(i->second);
		}
	}

	sort(landlessCountries.begin(), landlessCountries.end(), compareLandlessNationsAges);

	while ( (excess > 0) && (landlessCountries.size() > 0) )
	{
		world.removeCountry(landlessCountries.back()->getTag());
		landlessCountries.pop_back();
		excess--;
	}
}


void removeLandlessNations(EU3World& world)
{
	map<string, EU3Country*> countries = world.getCountries();

	for (map<string, EU3Country*>::iterator i = countries.begin(); i != countries.end(); i++)
	{
		vector<EU3Province*> provinces = i->second->getProvinces();
		if (provinces.size() == 0)
		{
			world.removeCountry(i->first);
		}
	}
}


void initStateMap(Object* obj, stateMapping& stateMap, stateIndexMapping& stateIndexMap)
{
	vector<Object*> leaves = obj->getLeaves();

	for (unsigned int i = 0; i < leaves.size(); i++)
	{
		vector<string> provinces = leaves[i]->getTokens();
		vector<int>		neighbors;

		for (vector<string>::iterator j = provinces.begin(); j != provinces.end(); j++)
		{
			neighbors.push_back( atoi(j->c_str()) );
			stateIndexMap.insert( make_pair(atoi(j->c_str()), i) );
		}
		for (vector<int>::iterator j = neighbors.begin(); j != neighbors.end(); j++)
		{
			stateMap.insert(make_pair(*j, neighbors));
		}
	}
}


cultureMapping initCultureMap(Object* obj) // TODO: consider cleaning up the distinguishers
{
	cultureMapping cultureMap;
	vector<Object*> links = obj->getLeaves();

	for (vector<Object*>::iterator i = links.begin(); i != links.end(); i++)
	{
		vector<Object*>			cultures	= (*i)->getLeaves();

		vector<string>				srcCultures;
		string						dstCulture;
		vector< distinguisher > distinguishers;
		for (vector<Object*>::iterator j = cultures.begin(); j != cultures.end(); j++)
		{
			if ( (*j)->getKey() == "vic" )
			{
				dstCulture = (*j)->getLeaf();
			}
			if ( (*j)->getKey() == "eu3" )
			{
				srcCultures.push_back( (*j)->getLeaf() );
			}
			if ( (*j)->getKey() == "owner" )
			{
				distinguisher newD;
				newD.first	= DTOwner;
				newD.second	= (*j)->getLeaf();
				distinguishers.push_back(newD);
			}
			if ( (*j)->getKey() == "religion" )
			{
				distinguisher newD;
				newD.first	= DTReligion;
				newD.second	= (*j)->getLeaf();
				distinguishers.push_back(newD);
			}
		}

		for (vector<string>::iterator j = srcCultures.begin(); j != srcCultures.end(); j++)
		{
			cultureStruct rule;
			rule.srcCulture		= (*j);
			rule.dstCulture		= dstCulture;
			rule.distinguishers	= distinguishers;
			cultureMap.push_back(rule);
		}
	}

	return cultureMap;
}


religionMapping initReligionMap(Object* obj)
{
	religionMapping religionMap;
	vector<Object*> links = obj->getLeaves();

	for (vector<Object*>::iterator i = links.begin(); i != links.end(); i++)
	{
		vector<Object*>	religions	= (*i)->getLeaves();
		string				dstReligion;
		vector<string>		srcReligion;

		for (vector<Object*>::iterator j = religions.begin(); j != religions.end(); j++)
		{
			if ( (*j)->getKey() == "vic" )
			{
				dstReligion = (*j)->getLeaf();
			}
			if ( (*j)->getKey() == "eu3" )
			{
				srcReligion.push_back( (*j)->getLeaf() );
			}
		}

		for (vector<string>::iterator j = srcReligion.begin(); j != srcReligion.end(); j++)
		{
			religionMap.insert(make_pair((*j), dstReligion));
		}
	}

	return religionMap;
}


unionMapping initUnionMap(Object* obj)
{
	unionMapping unionMap;

	vector<Object*> unions = obj->getLeaves();
	for (vector<Object*>::iterator i = unions.begin(); i != unions.end(); i++)
	{
		string tag;
		string culture;

		vector<Object*> aUnion = (*i)->getLeaves();
		for (vector<Object*>::iterator j = aUnion.begin(); j != aUnion.end(); j++)
		{
			if ( (*j)->getKey() == "tag" )
			{
				tag = (*j)->getLeaf();
			}
			if ( (*j)->getKey() == "culture" )
			{
				culture = (*j)->getLeaf();
			}
		}

		unionMap.push_back(make_pair(culture, tag));
	}

	return unionMap;
}


governmentMapping initGovernmentMap(Object* obj)
{
	governmentMapping governmentMap;
	vector<Object*> links = obj->getLeaves();

	for (vector<Object*>::iterator i = links.begin(); i != links.end(); i++)
	{
		vector<Object*>	governments	= (*i)->getLeaves();
		string				dstGovernment;
		vector<string>		srcGovernments;

		for (vector<Object*>::iterator j = governments.begin(); j != governments.end(); j++)
		{
			if ( (*j)->getKey() == "vic" )
			{
				dstGovernment = (*j)->getLeaf();
			}
			if ( (*j)->getKey() == "eu3" )
			{
				srcGovernments.push_back( (*j)->getLeaf() );
			}
		}

		for (vector<string>::iterator j = srcGovernments.begin(); j != srcGovernments.end(); j++)
		{
			governmentMap.insert(make_pair((*j), dstGovernment));
		}
	}

	return governmentMap;
}


void initUnionCultures(Object* obj, unionCulturesMap& unionCultures)
{
	vector<Object*> cultureGroups = obj->getLeaves();

	for (vector<Object*>::iterator i = cultureGroups.begin(); i != cultureGroups.end(); i++)
	{
		vector<Object*>		culturesObj		= (*i)->getLeaves();
		bool						hasUnion			= false;
		string					tag;
		vector<string>			cultures;

		for (vector<Object*>::iterator j = culturesObj.begin(); j != culturesObj.end(); j++)
		{
			if ( (*j)->getKey() == "union")
			{
				hasUnion	= true;
				tag		= (*j)->getLeaf();
			}
			else if ( (*j)->getKey() == "dynasty_names" )
			{
				continue;
			}
			else
			{
				cultures.push_back( (*j)->getKey() );
			}
		}

		if (hasUnion)
		{
			unionCultures[tag] = cultures;
		}
	}
}