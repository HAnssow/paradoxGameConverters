/*Copyright (c) 2016 The Paradox Game Converters Project

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.*/



#include "EU4World.h"
#include <algorithm>
#include <fstream>
#include "Log.h"
#include "OSCompatibilityLayer.h"
#include "../Configuration.h"
#include "../Mappers/CultureMapper.h"
#include "../Mappers/EU4CultureGroupMapper.h"
#include "../Mappers/ProvinceMapper.h"
#include "../Mappers/ReligionMapper.h"
#include "Object.h"
#include "ParadoxParserUTF8.h"
#include "EU4Province.h"
#include "EU4Country.h"
#include "EU4Diplomacy.h"
#include "EU4Version.h"
#include "EU4Localisation.h"
#include "EU4Religion.h"
#include <set>
using namespace std;



EU4World::EU4World(const string& EU4SaveFileName, map<string, string> possibleMods)
{
	LOG(LogLevel::Info) << "* Importing EU4 save *";

	//	Parse EU4 Save
	LOG(LogLevel::Info) << "Parsing save";
	Object* obj = parser_UTF8::doParseFile(EU4SaveFileName.c_str());
	if (obj == NULL)
	{
		LOG(LogLevel::Error) << "Could not parse file " << EU4SaveFileName;
		exit(-1);
	}

	LOG(LogLevel::Debug) << "Get EU4 Mod";
	vector<Object*> modObj = obj->getValue("mod_enabled");	// the used mods
	if (modObj.size() > 0)
	{
		string modString = modObj[0]->getLeaf();	// the names of all the mods
		while (modString != "")
		{
			string newMod;	// the corrected name of the mod
			const int firstQuote = modString.find("\"");	// the location of the first quote, defining the start of a mod name
			if (firstQuote == std::string::npos)
			{
				newMod.clear();
				modString.clear();
			}
			else
			{
				const int secondQuote = modString.find("\"", firstQuote + 1);	// the location of the second quote, defining the end of a mod name
				if (secondQuote == std::string::npos)
				{
					newMod.clear();
					modString.clear();
				}
				else
				{
					newMod = modString.substr(firstQuote + 1, secondQuote - firstQuote - 1);
					modString = modString.substr(secondQuote + 1, modString.size());
				}
			}

			if (newMod != "")
			{
				map<string, string>::iterator modItr = possibleMods.find(newMod);
				if (modItr != possibleMods.end())
				{
					string newModPath = modItr->second;	// the path for this mod
					if (Utils::DoesFileExist(newModPath))
					{
						LOG(LogLevel::Error) << newMod << " could not be found in the specified mod directory - a valid mod directory must be specified. Tried " << newModPath;
						exit(-1);
					}
					else
					{
						LOG(LogLevel::Debug) << "EU4 Mod is at " << newModPath;
						Configuration::addEU4Mod(newModPath);
					}
				}
				else
				{
					LOG(LogLevel::Error) << "No path could be found for " << newMod;
					exit(-1);
				}
			}
		}
	}

	LOG(LogLevel::Info) << "Building world";

	vector<Object*> versionObj = obj->getValue("savegame_version");	// the version of the save
	(versionObj.size() > 0) ? version = new EU4Version(versionObj[0]) : version = new EU4Version();
	Configuration::setEU4Version(*version);

	vector<Object*> enabledDLCsObj = obj->getValue("dlc_enabled");
	if (enabledDLCsObj.size() > 0)
	{
		vector<string>	activeDLCs;
		vector<string> DLCsObj = enabledDLCsObj[0]->getTokens();
		for (auto DLCsItr: DLCsObj)
		{
			activeDLCs.push_back(DLCsItr);
		}

		Configuration::setActiveDLCs(activeDLCs);
	}

	vector<Object*> dateObj = obj->getValue("date");
	if (dateObj.size() > 0)
	{
		date endDate(dateObj[0]->getLeaf());
		Configuration::setLastEU4Date(endDate);
	}

	string emperor;
	vector<Object*> emperorObj = obj->getValue("emperor");
	if (emperorObj.size() > 0)
	{
		emperor = emperorObj[0]->getLeaf();
	}

	// Use map/definition.csv to determine valid provinces
	map<int, int> validProvinces;
	ifstream definitionFile((Configuration::getEU4Path() + "/map/definition.csv").c_str());
	if (!definitionFile.is_open())
	{
		LOG(LogLevel::Error) << "Could not open map/definition.csv";
		exit(-1);
	}
	char input[256];
	while (!definitionFile.eof())
	{
		definitionFile.getline(input, 255);
		string inputStr(input);
		int dbgprovNum = atoi(inputStr.substr(0, inputStr.find_first_of(';')).c_str());
		if (
				(inputStr.substr(0, 8) == "province") ||
				(inputStr.substr(inputStr.find_last_of(';') + 1, 6) == "Unused") ||
				(inputStr.substr(inputStr.find_last_of(';') + 1, 3) == "RNW")
			)
		{
			continue;
		}
		int provNum = atoi(inputStr.substr(0, inputStr.find_first_of(';')).c_str());
		validProvinces.insert(make_pair(provNum, provNum));
	}

	// Get Provinces
	provinces.clear();
	vector<Object*> provincesObj = obj->getValue("provinces");					// the object holding the provinces
	if (provincesObj.size() > 0)
	{
		vector<Object*> provincesLeaves = provincesObj[0]->getLeaves();		// the objects holding the individual provinces
		for (unsigned int j = 0; j < provincesLeaves.size(); j++)
		{
			string keyProv = (provincesLeaves[j])->getKey();						// the key for the province

			if (
					(atoi(keyProv.c_str()) < 0) &&													// Check if key is a negative value (EU4 style)
					(validProvinces.find(-1 * atoi(keyProv.c_str())) != validProvinces.end())	// check it's a valid province for this version of EU4
				)
			{
				EU4Province* province = new EU4Province((provincesLeaves[j]));	// the province in our format
				provinces.insert(make_pair(province->getNum(), province));
			}
		}
	}

	// Get Countries
	countries.clear();
	vector<Object*> countriesObj = obj->getValue("countries");				// the object holding the countries
	if (countriesObj.size() > 0)
	{
		vector<Object*> countriesLeaves = countriesObj[0]->getLeaves();	// the objects holding the countries themselves
		for (unsigned int j = 0; j < countriesLeaves.size(); j++)
		{
			string keyCoun = countriesLeaves[j]->getKey();						// the key for this country

			if ((keyCoun == "---") || (keyCoun == "REB") || (keyCoun == "PIR") || (keyCoun == "NAT"))
			{
				continue;
			}
			else
			{
				EU4Country* country = new EU4Country(countriesLeaves[j], version);	// the country in our format
				countries.insert(make_pair(country->getTag(), country));

				// set HRE stuff
				auto capitalItr = provinces.find(country->getCapital());
				if ((capitalItr != provinces.end()) && (capitalItr->second->getInHRE()))
				{
					country->setInHRE(true);
				}
				if (country->getTag() == emperor)
				{
					country->setEmperor(true);
				}
			}
		}
	}


	vector<Object*> revolutionTargetObj = obj->getValue("revolution_target");
	if (revolutionTargetObj.size() > 0)
	{
		string revolutionTarget = revolutionTargetObj[0]->getLeaf();
		auto country = countries.find(revolutionTarget);
		if (country != countries.end())
		{
			country->second->viveLaRevolution(true);
		}
	}

	// add province owner info to countries
	for (map<int, EU4Province*>::iterator i = provinces.begin(); i != provinces.end(); i++)
	{
		map<string, EU4Country*>::iterator j = countries.find( i->second->getOwnerString() );
		if (j != countries.end())
		{
			j->second->addProvince(i->second);
			i->second->setOwner(j->second);
		}
	}

	// add province core info to countries
	for (map<int, EU4Province*>::iterator i = provinces.begin(); i != provinces.end(); i++)
	{
		vector<EU4Country*> cores = i->second->getCores(countries);	// the cores held on this province
		for (vector<EU4Country*>::iterator j = cores.begin(); j != cores.end(); j++)
		{
			(*j)->addCore(i->second);
		}
	}

	vector<Object*> diploObj = obj->getValue("diplomacy");	// the object holding the world's diplomacy
	if (diploObj.size() > 0)
	{
		diplomacy = new EU4Diplomacy(diploObj[0]);
	}
	else
	{
		diplomacy = new EU4Diplomacy;
	}

	// calculate total province weights
	worldWeightSum = 0;

	/*ofstream EU4_Production("EU4_Production.csv");
	ofstream EU4_Tax("EU4_TaxIncome.csv");
	ofstream EU4_World("EU4_World.csv");*/

	std::vector<double> provEconVec;

	std::map<string, vector<double> > world_tag_weights;

	//// Heading
	//EU4_Production << "PROV NAME" << ",";
	//EU4_Production << "OWNER" << ",";
	//EU4_Production << "TRADE GOOD" << ",";
	//EU4_Production << "GOODS PROD" << ",";
	//EU4_Production << "PRICE" << ",";
	//EU4_Production << "TRADE EFF" << ",";
	//EU4_Production << "PROD EFF" << ",";
	//EU4_Production << "PROV TRADE VAL" << ",";
	//EU4_Production << "TOTAL TRADE VAL" << ",";
	//EU4_Production << "TOTAL PRODUCTION" << endl;

	//// Heading
	//EU4_World << "COUNTRY" << ",";
	//EU4_World << "BASE TAX (2x)" << ",";
	//EU4_World << "TAX INCOME" << ",";
	//EU4_World << "PRODUCTION" << ",";
	//EU4_World << "BUILDINGS" << ",";
	//EU4_World << "MANPOWER" << ",";
	//EU4_World << "SUBTOTAL SAN BUILD" << ",";
	//EU4_World << "TOTAL WEIGHT" << endl;

	//// Heading
	//EU4_Tax << "PROV NAME" << ",";
	//EU4_Tax << "OWNER" << ",";
	//EU4_Tax << "BASE TAX" << ",";
	//EU4_Tax << "BUILD INCOME" << ",";
	//EU4_Tax << "TAX EFF" << ",";
	//EU4_Tax << "TOTAL TAX INCOME" << endl;
	for (map<int, EU4Province*>::iterator i = provinces.begin(); i != provinces.end(); i++)
	{
		i->second->determineProvinceWeight();
		// 0: Goods produced; 1 trade goods price; 2: trade value efficiency; 3: production effiency; 4: trade value; 5: production income
		// 6: base tax; 7: building tax income 8: building tax eff; 9: total tax income; 10: total_trade_value


		provEconVec = i->second->getProvProductionVec();
		/*EU4_Production << i->second->getProvName() << ",";
		EU4_Production << i->second->getOwnerString() << ",";
		EU4_Production << i->second->getTradeGoods() << ",";
		EU4_Production << provEconVec.at(0) << ",";
		EU4_Production << provEconVec.at(1) << ",";
		EU4_Production << provEconVec.at(2) << ",";
		EU4_Production << provEconVec.at(3) << ",";
		EU4_Production << provEconVec.at(4) << ",";
		EU4_Production << provEconVec.at(10) << ",";
		EU4_Production << i->second->getProvProdIncome() << "," << endl;


		EU4_Tax << i->second->getProvName() << ",";
		EU4_Tax << i->second->getOwnerString() << ",";
		EU4_Tax << provEconVec.at(6) << ",";
		EU4_Tax << provEconVec.at(7) << ",";
		EU4_Tax << provEconVec.at(8) << ",";
		EU4_Tax << provEconVec.at(9) << "," << endl;*/

		worldWeightSum += i->second->getTotalWeight();

		vector<double> map_values;
		// Total Base Tax, Total Tax Income, Total Production, Total Buildings, Total Manpower, total province weight //
		map_values.push_back((2 * i->second->getBaseTax()));
		map_values.push_back(i->second->getProvTaxIncome());
		map_values.push_back(i->second->getProvProdIncome());
		map_values.push_back(i->second->getProvTotalBuildingWeight());
		map_values.push_back(i->second->getProvMPWeight());
		map_values.push_back(i->second->getTotalWeight());
		
		if (world_tag_weights.count(i->second->getOwnerString())) {
			vector<double> new_map_values;
			new_map_values = world_tag_weights[i->second->getOwnerString()];
			new_map_values[0] += map_values[0];
			new_map_values[1] += map_values[1];
			new_map_values[2] += map_values[2];
			new_map_values[3] += map_values[3];
			new_map_values[4] += map_values[4];
			new_map_values[5] += map_values[5];

			world_tag_weights[i->second->getOwnerString()] = new_map_values;
			
		}
		else {
			world_tag_weights.insert(std::pair<string, vector<double> >(i->second->getOwnerString(), map_values));
		}

	}
	LOG(LogLevel::Info) << "Sum of all Province Weights: " << worldWeightSum;

	// Total Base Tax, Total Tax Income, Total Production, Total Buildings, Total Manpower, total province weight //
	LOG(LogLevel::Info) << "World Tag Map Size: " << world_tag_weights.size();

	/*for (map<string, vector<double> >::iterator i = world_tag_weights.begin(); i != world_tag_weights.end(); i++)
	{
		EU4_World << i->first << ",";
		EU4_World << i->second[0] << ",";
		EU4_World << i->second[1] << ",";
		EU4_World << i->second[2] << ",";
		EU4_World << i->second[3] << ",";
		EU4_World << i->second[4] << ",";
		EU4_World << (i->second[5] - i->second[3]) << ",";
		EU4_World << i->second[5] << endl;
	}

	EU4_Production.close();
	EU4_Tax.close();
	EU4_World.close();*/

	checkAllEU4CulturesMapped();
	readCommonCountries();
	setLocalisations();
	resolveRegimentTypes();
	mergeNations();
	checkAllProvincesMapped();
	setNumbersOfDestinationProvinces();

	EU4Religion::createSelf();
	checkAllEU4ReligionsMapped();

	removeEmptyNations();
	if (Configuration::getRemovetype() == "dead")
	{
		removeDeadLandlessNations();
	}
	else if (Configuration::getRemovetype() == "all")
	{
		removeLandlessNations();
	}
}


void EU4World::setNumbersOfDestinationProvinces()
{
	for (auto province: provinces)
	{
		auto Vic2Provinces = provinceMapper::getVic2ProvinceNumbers(province.first);
		province.second->setNumDestV2Provs(Vic2Provinces.size());
	}
}


void EU4World::readCommonCountries()
{
	LOG(LogLevel::Info) << "Reading EU4 common/countries";
	ifstream commonCountries(Configuration::getEU4Path() + "/common/country_tags/00_countries.txt");	// the data in the countries file
	readCommonCountriesFile(commonCountries, Configuration::getEU4Path());
	for (auto itr: Configuration::getEU4Mods())
	{
		set<string> fileNames;
		Utils::GetAllFilesInFolder(itr + "/common/country_tags/", fileNames);
		for (set<string>::iterator fileItr = fileNames.begin(); fileItr != fileNames.end(); fileItr++)
		{
			ifstream convertedCommonCountries(itr + "/common/country_tags/" + *fileItr);	// a stream of the data in the converted countries file
			readCommonCountriesFile(convertedCommonCountries, itr);
		}
	}
}


void EU4World::readCommonCountriesFile(istream& in, const std::string& rootPath)
{
	// Add any info from common\countries
	const int maxLineLength = 10000;	// the maximum line length
	char line[maxLineLength];			// the line being processed

	while (true)
	{
		in.getline(line, maxLineLength);
		if (in.eof())
		{
			return;
		}
		std::string countryLine = line;
		if (countryLine.size() >= 6 && countryLine[0] != '#')
		{
			// First three characters must be the tag.
			std::string tag = countryLine.substr(0, 3);
			map<string, EU4Country*>::iterator findIter = countries.find(tag);
			if (findIter != countries.end())
			{
				EU4Country* country = findIter->second;

				// The country file name is all the text after the equals sign (possibly in quotes).
				size_t commentPos	= countryLine.find('#', 3);
				if (commentPos != string::npos)
				{
					countryLine = countryLine.substr(0, commentPos);
				}
				size_t equalPos	= countryLine.find('=', 3);
				size_t beginPos	= countryLine.find_first_not_of(' ', equalPos + 1);
				size_t endPos		= countryLine.find_last_of('\"') + 1;
				std::string fileName = countryLine.substr(beginPos, endPos - beginPos);
				if (fileName.front() == '"' && fileName.back() == '"')
				{
					fileName = fileName.substr(1, fileName.size() - 2);
				}
				std::replace(fileName.begin(), fileName.end(), '/', '/');

				// Parse the country file.
				std::string path = rootPath + "/common/" + fileName;
				size_t lastPathSeparatorPos = path.find_last_of('/');
				std::string localFileName = path.substr(lastPathSeparatorPos + 1, string::npos);
				country->readFromCommonCountry(localFileName, parser_UTF8::doParseFile(path.c_str()));
			}
		}
	}
}


EU4Country* EU4World::getCountry(string tag) const
{
	map<string, EU4Country*>::const_iterator i = countries.find(tag);
	return (i != countries.end()) ? i->second : NULL;
}


EU4Province* EU4World::getProvince(const int provNum) const
{
	map<int, EU4Province*>::const_iterator i = provinces.find(provNum);
	return (i != provinces.end()) ? i->second : NULL;
}


void EU4World::resolveRegimentTypes()
{
	LOG(LogLevel::Info) << "Resolving unit types.";
	RegimentTypeMap rtm;
	fstream read;
	read.open("unit_strength.txt");
	if (read.is_open())
	{
		read.close();
		read.clear();
		LOG(LogLevel::Info) << "\tReading unit strengths from unit_strength.txt";
		Object* unitsObj = parser_UTF8::doParseFile("unit_strength.txt");
		if (unitsObj == NULL)
		{
			LOG(LogLevel::Error) << "Could not parse file unit_strength.txt";
			exit(-1);
		}
		for (int i = 0; i < num_reg_categories; ++i)
		{
			AddCategoryToRegimentTypeMap(unitsObj, (RegimentCategory)i, RegimentCategoryNames[i], rtm);
		}
	}
	else
	{
		LOG(LogLevel::Info) << "\tReading unit strengths from EU4 installation folder";

		set<string> filenames;
		Utils::GetAllFilesInFolder(Configuration::getEU4Path() + "/common/units/", filenames);
		for (auto filename: filenames)
		{
			AddUnitFileToRegimentTypeMap((Configuration::getEU4Path() + "/common/units"), filename, rtm);
		}
	}
	read.close();
	read.clear();

	for (map<string, EU4Country*>::iterator itr = countries.begin(); itr != countries.end(); ++itr)
	{
		itr->second->resolveRegimentTypes(rtm);
	}
}


void EU4World::checkAllProvincesMapped() const
{
	for (auto province: provinces)
	{
		auto Vic2Provinces = provinceMapper::getVic2ProvinceNumbers(province.first);
		if (Vic2Provinces.size() == 0)
		{
			LOG(LogLevel::Warning) << "No mapping for province " << province.first;
		}
	}
}


void EU4World::checkAllEU4CulturesMapped() const
{
	for (auto cultureItr: EU4CultureGroupMapper::getCultureToGroupMap())
	{
		string Vi2Culture;

		string	EU4Culture	= cultureItr.first;
		bool		matched		= cultureMapper::cultureMatch(EU4Culture, Vi2Culture);
		if (!matched)
		{
			LOG(LogLevel::Warning) << "No culture mapping for EU4 culture " << EU4Culture;
		}
	}
}


void EU4World::checkAllEU4ReligionsMapped() const
{
	for (auto EU4Religion: EU4Religion::getAllReligions())
	{
		auto Vic2Religion = religionMapper::getVic2Religion(EU4Religion.first);
		if (Vic2Religion == "")
		{
			Log(LogLevel::Warning) << "No religion mapping for EU4 religion " << EU4Religion.first;
		}
	}
}


void EU4World::setLocalisations()
{
	LOG(LogLevel::Info) << "Reading localisation";
	EU4Localisation localisation;
	localisation.ReadFromAllFilesInFolder(Configuration::getEU4Path() + "/localisation");
	for (auto itr: Configuration::getEU4Mods())
	{
		LOG(LogLevel::Debug) << "Reading mod localisation";
		localisation.ReadFromAllFilesInFolder(itr + "/localisation");
	}

	for (map<string, EU4Country*>::iterator countryItr = countries.begin(); countryItr != countries.end(); countryItr++)
	{
		const auto& nameLocalisations = localisation.GetTextInEachLanguage(countryItr->second->getTag());	// the names in all languages
		for (const auto& nameLocalisation : nameLocalisations)	// the name under consideration
		{
			const std::string& language = nameLocalisation.first;	// the language
			const std::string& name = nameLocalisation.second;		// the name of the country in this language
			countryItr->second->setLocalisationName(language, name);
		}
		const auto& adjectiveLocalisations = localisation.GetTextInEachLanguage(countryItr->second->getTag() + "_ADJ");	// the adjectives in all languages
		for (const auto& adjectiveLocalisation : adjectiveLocalisations)	// the adjective under consideration
		{
			const std::string& language = adjectiveLocalisation.first;		// the language
			const std::string& adjective = adjectiveLocalisation.second;	// the adjective for the country in this language
			countryItr->second->setLocalisationAdjective(language, adjective);
		}
	}
}


// todo: move the getting of rules into its own mapper, with a merge rule structure type
//		then break out things into subfunctions with a give rule as a parameter
void EU4World::mergeNations()
{
	LOG(LogLevel::Info) << "Merging nations";
	Object* mergeObj = parser_UTF8::doParseFile("merge_nations.txt");
	if (mergeObj == NULL)
	{
		LOG(LogLevel::Error) << "Could not parse file merge_nations.txt";
		exit(-1);
	}

	vector<Object*> rules = mergeObj->getValue("merge_nations");
	if (rules.size() < 0)
	{
		LOG(LogLevel::Debug) << "No nations have merging requested (skipping)";
		return;
	}

	rules = rules[0]->getLeaves();
	for (auto rule: rules)
	{
		if ((rule->getKey() == "merge_daimyos") && (rule->getLeaf() == "yes"))
		{
			uniteJapan();
			continue;
		}

		vector<Object*> ruleItems = rule->getLeaves();

		string masterTag;
		vector<string> slaveTags;
		bool enabled = false;
		for (auto item: ruleItems)
		{
			if ((item->getKey() == "merge") && (item->getLeaf() == "yes"))
			{
				enabled = true;
			}
			else if (item->getKey() == "master")
			{
				masterTag = item->getLeaf();
			}
			else if (item->getKey() == "slave")
			{
				slaveTags.push_back(item->getLeaf());
			}
		}

		EU4Country* master = getCountry(masterTag);
		if (enabled && (master != NULL))
		{
			for (auto slaveTag: slaveTags)
			{
				auto slave = getCountry(slaveTag);
				if (slave != NULL)
				{
					master->eatCountry(slave);
				}
			}
		}
	}
}


void EU4World::uniteJapan()
{
	EU4Country* japan = getCountry("JAP");
	if (japan == NULL)
	{
		return;
	}
	if (japan->hasFlag("united_daimyos_of_japan"))
	{
		return;
	}

	for (auto country: countries)
	{
		if (country.second->getPossibleDaimyo())
		{
			japan->eatCountry(country.second);
		}
	}
}


void EU4World::removeEmptyNations()
{
	map<string, EU4Country*> survivingCountries;

	for (auto country: countries)
	{
		vector<EU4Province*> provinces = country.second->getProvinces();
		vector<EU4Province*> cores = country.second->getCores();
		if ((provinces.size() == 0) && (cores.size() == 0))
		{
			LOG(LogLevel::Debug) << "Removing empty nation " << country.first;
		}
		else
		{
			survivingCountries.insert(country);
		}
	}

	countries.swap(survivingCountries);
}


void EU4World::removeDeadLandlessNations()
{
	map<string, EU4Country*> landlessCountries;
	for (auto country: countries)
	{
		vector<EU4Province*> provinces = country.second->getProvinces();
		if (provinces.size() == 0)
		{
			landlessCountries.insert(country);
		}
	}

	for (auto country: landlessCountries)
	{
		if (!country.second->cultureSurvivesInCores())
		{
			countries.erase(country.first);
			LOG(LogLevel::Debug) << "Removing dead landless nation " << country.first;
		}
	}
}


void EU4World::removeLandlessNations()
{
	map<string, EU4Country*> survivingCountries;

	for (auto country: countries)
	{
		auto provinces = country.second->getProvinces();
		if (provinces.size() == 0)
		{
			LOG(LogLevel::Debug) << "Removing landless nation " << country.first;
		}
		else
		{
			survivingCountries.insert(country);
		}
	}

	countries.swap(survivingCountries);
}