//djs
#include "llrfConfigReader.h"
#include "llrfStructs.h"
//stl
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <time.h>
#include <algorithm>
#include <ctype.h>
//____________________________________________________________________________________________________
llrfConfigReader::llrfConfigReader( const std::string & configFileLocation1,
                                        const bool startVirtualMachine,
                                        const bool* show_messages_ptr,
                                        const bool* show_debug_messages_ptr ):
configReader( configFileLocation1, show_messages_ptr, show_debug_messages_ptr ),
usingVirtualMachine(startVirtualMachine),
lastPVstruct(nullptr)
{
//    std::cout << configFileLocation1 << std::endl;
}
//______________________________________________________________________________
llrfConfigReader::~llrfConfigReader(){}
//______________________________________________________________________________
bool llrfConfigReader::readConfig()
{
    debugMessage( "\n", "**** Attempting to Read The RF Config File ****" );

    std::string line, trimmedLine;
    bool success = false;

    pvComStructs.clear();
    pvMonStructs.clear();
    std::ifstream inputFile;

    inputFile.open( configFile1, std::ios::in );
    if( inputFile )
    {
        bool readingData       = false;
        bool readingObjs       = false;
        bool readingCommandPVs = false;
        bool readingMonitorPVs = false;

        debugMessage( "Opened from ", configFile1 );
        while( std::getline( inputFile, line ) ) /// Go through, reading file line by line
        {
            trimmedLine = trimAllWhiteSpace( trimToDelimiter( line, UTL::END_OF_LINE ) );
            if( trimmedLine.size() > 0 )
            {
                if( stringIsSubString( line, UTL::END_OF_DATA ) )
                {
                    debugMessage( "Found END_OF_DATA" );
                    readingData = false;
                    readingObjs = false;
                    readingCommandPVs  = false;
                    readingMonitorPVs  = false;
                    break;
                }
                if( readingData )
                {
                    if( stringIsSubString( trimmedLine, UTL::VERSION ) )
                        getVersion( trimmedLine );
                    else if( stringIsSubString( trimmedLine, UTL::NUMBER_OF_OBJECTS ) )
                        getNumObjs( trimmedLine );
                    else if( stringIsSubString( trimmedLine, UTL::NUMBER_OF_ILOCKS ) )
                        getNumIlocks( trimmedLine );
                    else
                    {
                        switch( configVersion )
                        {
                            case 1:
                                if( trimmedLine.find_first_of( UTL::EQUALS_SIGN ) != std::string::npos )
                                {
                                    std::vector<std::string> keyVal = getKeyVal( trimmedLine );

                                    if( readingObjs )
                                        addToLLRFObjectsV1( keyVal );

                                    else if ( readingCommandPVs  )
                                        addToPVCommandMapV1( keyVal );

                                    else if ( readingMonitorPVs )
                                        addToPVMonitorMapV1( keyVal );
                                }
                                break;
                            default:
                                message( "!!!!!WARNING DID NOT FIND CONFIG FILE VERSION NUMBER!!!!!!"  );
                        }
                    }
                }
                if( stringIsSubString( line, UTL::START_OF_DATA ) )
                {
                    readingData = true;
                    debugMessage( "Found START_OF_DATA" );
                }
                if( stringIsSubString( line, UTL::PV_COMMANDS_START ) )
                {
                    readingCommandPVs  = true;
                    readingObjs = false;
                    readingMonitorPVs = false;
                    debugMessage( "Found PV_COMMANDS_START" );
                }
                if( stringIsSubString( line, UTL::PV_MONITORS_START ) )
                {
                    readingCommandPVs = false;
                    readingObjs       = false;
                    readingMonitorPVs = true;
                    debugMessage( "Found PV_MONITORS_START" );
                }
                if( stringIsSubString( line, UTL::OBJECTS_START ) )
                {
                    readingObjs        = true;
                    readingCommandPVs  = false;
                    readingMonitorPVs  = false;
                    debugMessage( "Found OBJECTS_START" );
                }
            }
        }
        inputFile.close( );
        debugMessage( "File Closed" );

//        if( numObjs == laserObjects.size() )
//            debugMessage( "*** Created ", numObjs, " Shutter Objects, As Expected ***" );
//        else
//            debugMessage( "*** Created ", laserObjects.size() ," Expected ", numObjs,  " ERROR ***"  );

        success = true;
    }
    else{
        message( "!!!! Error Can't Open Shutter Config File after searching in:  ", configFile1, " !!!!"  );
    }
    return success;
    return false;
}
//______________________________________________________________________________
void llrfConfigReader::addToPVMonitorMapV1(const std::vector<std::string>& keyVal)
{
    addToPVMapV1(keyVal);
}
//______________________________________________________________________________
void llrfConfigReader::addToPVCommandMapV1( const  std::vector<std::string> &keyVal  )
{
    addToPVMapV1(keyVal);
}
//______________________________________________________________________________
void llrfConfigReader::addToPVMapV1(const std::vector<std::string>& keyVal)
{// here i've hardcoded in the monitor or command PVs
    if( stringIsSubString(keyVal[0],"SUFFIX"))
    {
        // there are different types of screen, here we are hardcoding in how to interpret the config file and where to put
        // each type of monitor.
        if( keyVal[0] == UTL::PV_SUFFIX_AMPR)
        {
            addToPVStruct( pvMonStructs, llrfStructs::LLRF_PV_TYPE::AMP_R,keyVal[1]);
        }
        else if(keyVal[0] == UTL::PV_SUFFIX_PHI)
        {
            addToPVStruct(pvMonStructs, llrfStructs::LLRF_PV_TYPE::PHI,keyVal[1]);
        }
        else if(keyVal[0] == UTL::PV_SUFFIX_AMPW)
        {
            addToPVStruct(pvMonStructs, llrfStructs::LLRF_PV_TYPE::AMP_W,keyVal[1]);
        }
        else if(keyVal[0] == UTL::PV_SUFFIX_AMP_MVM)
        {
            addToPVStruct(pvMonStructs, llrfStructs::LLRF_PV_TYPE::AMP_MVM,keyVal[1]);
        }
        else if(keyVal[0] == UTL::PV_SUFFIX_PHI_DEG)
        {
            addToPVStruct(pvMonStructs, llrfStructs::LLRF_PV_TYPE::PHI_DEG,keyVal[1]);
        }
    }
    else
    {   // we assume the order in the config file is correct
        if( keyVal[0] == UTL::PV_COUNT )
            //lastPVstruct.back().COUNT = getCOUNT( keyVal[ 1 ] );
            lastPVstruct->COUNT = getCOUNT( keyVal[ 1 ] );
        else if( keyVal[0] == UTL::PV_MASK )
            //pvMonStructs.back().MASK = getMASK( keyVal[ 1 ] );
            lastPVstruct->MASK = getMASK( keyVal[ 1 ] );
        else if( keyVal[0] == UTL::PV_CHTYPE )
            lastPVstruct->CHTYPE = getCHTYPE( keyVal[ 1 ] );
    }
}
//______________________________________________________________________________
void llrfConfigReader::addToPVStruct(std::vector<llrfStructs::pvStruct>& pvs,const llrfStructs::LLRF_PV_TYPE pvtype,const std::string& pvSuffix)
{
    pvs.push_back( llrfStructs::pvStruct() );
    pvs.back().pvType      = pvtype;
    pvs.back().pvSuffix    = pvSuffix;
    debugMessage("Added ", pvs.back().pvSuffix, " suffix for ", ENUM_TO_STRING( pvs.back().pvType) );
    lastPVstruct = &pvs.back();
}
//______________________________________________________________________________
void llrfConfigReader::addToLLRFObjectsV1( const std::vector<std::string> &keyVal   )
{
    if( keyVal[0] == UTL::NAME )
    {
        /// http://stackoverflow.com/questions/5914422/proper-way-to-initialize-c-structs
        /// init structs 'correctly'
        llrfObj.name = keyVal[ 1 ];
        llrfObj.numIlocks = numIlocks;
        debugMessage("Added ", llrfObj.name );
    }
    else if( keyVal[0] == UTL::PV_ROOT )
    {
        if( usingVirtualMachine )
            llrfObj.pvRoot =  UTL::VM_PREFIX + keyVal[ 1 ];
        else
            llrfObj.pvRoot = keyVal[ 1 ];
    }
    else if( keyVal[0] == UTL::LLRF_PHI_CALIBRATION )
    {
        llrfObj.phiCalibration = getNumD(keyVal[ 1 ]);
        debugMessage("LLRF phiCalibration = ", llrfObj.phiCalibration );
    }
    else if( keyVal[0] == UTL::LLRF_AMP_CALIBRATION )
    {
        llrfObj.ampCalibration = getNumD(keyVal[ 1 ]);
        debugMessage("LLRF ampCalibration = ", llrfObj.ampCalibration );
    }
    else if( keyVal[0] == UTL::LLRF_CREST_PHI )
    {
        llrfObj.crestPhi = getNumL(keyVal[ 1 ]);
    }
    // potentially we hardcode this value into the binary, and have it *NOT* configurable
    else if( keyVal[0] == UTL::LLRF_MAX_AMPLITUDE)
        llrfObj.maxAmp = getNumL(keyVal[ 1 ]);
}
//______________________________________________________________________________
bool llrfConfigReader::getllrfObject(llrfStructs::llrfObject& obj )
{
    obj = llrfObj;
    for(auto && it : pvMonStructs)
    {
        obj.pvMonStructs[it.pvType] = it;
    }
    for(auto && it : pvComStructs)
    {
        obj.pvComStructs[it.pvType] = it;
    }
    return true;
}

