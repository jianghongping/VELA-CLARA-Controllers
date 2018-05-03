///
/// Duncan Scott July 2015
///
/// For reading in parameters
/// Input files will be plain text
///
#ifndef magnetConfigReader_H_
#define magnetConfigReader_H_
// stl
#include <string>
#include <vector>
#include <map>
#include <iostream>
// me
#include "configReader.h"
#include "magnetStructs.h"


class magnetConfigReader : public configReader
{
    public:

        magnetConfigReader();

        magnetConfigReader::magnetConfigReader(const std::string&magConf,
                                                const bool startVirtualMachine,
                                                const bool& show_messages,
                                                const bool& show_debug_messages);

        ~magnetConfigReader();

        bool readConfig();
        bool getMagData(std::map<std::string, magnetStructs::magnetObject> & mapToFill);
        magnetStructs::degaussValues getDeguassStruct();

    private:

        /// for inj_mag objects there are 2 types of hardware, we should be able to read in
        /// the config files with 1 function and function pointers, this can't go in the
        /// base as the functions addToObjectsV1() etc. are in derived classes
        /// This seems like a "nice" use of function pointers (?)

        typedef void (magnetConfigReader::*aKeyValMemFn)(const std::vector<std::string> &keyVal);
        bool readConfig(magnetConfigReader & obj, const std::string & fn, aKeyValMemFn f1, aKeyValMemFn f2, aKeyValMemFn f3);

        magnetStructs::degaussValues degstruct;
        //void addToDegaussObj(const std::vector<std::string> &keyVal);


        //void copyPartOfMagObj(magnetStructs::magnetObject & to, magnetStructs::magnetObject & from);

        /// These are read into vectors as you can use the .back() member function, which i find handy.
        /// once all the data is read in we can construct the final map of objects to return to the epicsInterface

        // NR-PSU objects
        std::vector<magnetStructs::pvStruct> pvPSUMonStructs;
        std::vector<magnetStructs::pvStruct> pvPSUComStructs;

        // Magnet Objects
        std::vector<magnetStructs::magnetObject> magObjects;
        std::vector<magnetStructs::pvStruct> pvMagMonStructs;
        std::vector<magnetStructs::pvStruct> pvMagComStructs;

        void addToMagObjectsV1(const std::vector<std::string> &keyVal);
        void addToMagMonStructsV1(const std::vector<std::string> &keyVal);
        void addToMagComStructsV1(const std::vector<std::string> &keyVal);

        void addMagType(const std::vector<std::string> &keyVal);

        const std::string magConf;

       void addToPVStruct(std::vector<magnetStructs::pvStruct> & pvStruct_v, const  std::vector<std::string> &keyVal);
        void addCOUNT_MASK_OR_CHTYPE( std::vector<magnetStructs::pvStruct>  & pvStruct_v, const std::vector<std::string> &keyVal);

};
#endif //magnetConfigReader_H_