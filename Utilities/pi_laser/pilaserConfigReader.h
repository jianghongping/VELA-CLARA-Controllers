///
/// Duncan Scott July 2015
///
/// For reading in parameters
/// Input files will be plain text
///
#ifndef _PI_LASER_CONFIG_READER_H_
#define _PI_LASER_CONFIG_READER_H_
// stl
#include <string>
#include <vector>
#include <map>
// me
#include "configReader.h"
#include "pilaserStructs.h"


class pilaserConfigReader:public configReader
{
    public:
        pilaserConfigReader(const std::string& configFile,
                     const bool* show_messages,
                     const bool* show_debug_messages,
                     const bool usingVM);
        ~pilaserConfigReader();

//        bool readConfig( );
//        bool getpilaserObject(pilaserStructs::pilaserObject & obj);

    private:
//        pilaserStructs::pilaserObject pilaserObject;
//        std::vector<pilaserStructs::pvStruct> pvMonStructs;
//        std::vector<pilaserStructs::pvStruct> pvComStructs;
//
//        // hand pointer so we cna keep track of the last PV struct we were adding data to
//        //std::vector< pilaserStructs::pvStruct > * lastPVStruct;
//
//        void addToPVStruct(std::vector< pilaserStructs::pvStruct > & pvs, const pilaserStructs::PILASER_PV_TYPE pvtype, const std::string& pvSuffix);
//
//        void addTopilaserObjectsV1( const std::vector<std::string> &keyVal );
//        void addToPVCommandMapV1  ( const std::vector<std::string> &keyVal );
//        void addToPVMonitorMapV1  ( const std::vector<std::string> &keyVal );
//
//        const bool usingVirtualMachine;
};
#endif //_PI_LASER_CONFIG_READER_H_