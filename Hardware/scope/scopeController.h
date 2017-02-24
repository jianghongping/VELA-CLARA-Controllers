//              This file is part of VELA-CLARA-Controllers.                          //
//------------------------------------------------------------------------------------//
//    VELA-CLARA-Controllers is free software: you can redistribute it and/or modify  //
//    it under the terms of the GNU General Public License as published by            //
//    the Free Software Foundation, either version 3 of the License, or               //
//    (at your option) any later version.                                             //
//    VELA-CLARA-Controllers is distributed in the hope that it will be useful,       //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of                  //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                   //
//    GNU General Public License for more details.                                    //
//                                                                                    //
//    You should have received a copy of the GNU General Public License               //
//    along with VELA-CLARA-Controllers.  If not, see <http://www.gnu.org/licenses/>. //

#ifndef SCOPE_CONTROLLER_H
#define SCOPE_CONTROLLER_H

// project
#include "scopeInterface.h"
#include "controller.h"
// stl
#include <string>
#include <vector>

class scopeController : public controller
{
    public:

        /// we have overloaded constructors to specify config-file location

        scopeController();// const bool show_messages = true, const bool show_debug_messages = true );
        scopeController( const std::string &configFileLocation1, const std::string &configFileLocation2,
                         const bool show_messages, const bool show_debug_messages, const bool shouldStartEPICS,
                         const bool startVirtualMachine, const VELA_ENUM::MACHINE_AREA myMachineArea );
        ~scopeController();
//
//        bool hasTrig( const std::string & scopeName );
//        bool hasNoTrig( const std::string & scopeName );

        bool monitoringTraces;
        bool monitoringNums;
        bool isMonitoringScopeTrace( const std::string & scopeName, scopeStructs::SCOPE_PV_TYPE pvType );
        bool isMonitoringScopeNum( const std::string & scopeName, scopeStructs::SCOPE_PV_TYPE pvType );
        bool isNotMonitoringScopeTrace( const std::string & scopeName, scopeStructs::SCOPE_PV_TYPE pvType );
        bool isNotMonitoringScopeNum( const std::string & scopeName, scopeStructs::SCOPE_PV_TYPE pvType );

        double getScopeP1( const std::string & scopeName );
        double getScopeP2( const std::string & scopeName );
        double getScopeP3( const std::string & scopeName );
        double getScopeP4( const std::string & scopeName );
        double getWCMQ()   ;
        double getICT1Q()  ;
        double getICT2Q()  ;
        double getFCUPQ()  ;
        double getEDFCUPQ();
        const scopeStructs::scopeTraceData & getScopeTraceDataStruct( const std::string & scopeName );
        const scopeStructs::scopeNumObject & getScopeNumDataStruct( const std::string & scopeName );
        std::vector< std::vector< double > > getScopeTraces( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        std::vector< double > getScopeNums( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        std::vector< double > getScopeP1Vec( const std::string & name );
        std::vector< double > getScopeP2Vec( const std::string & name );
        std::vector< double > getScopeP3Vec( const std::string & name );
        std::vector< double > getScopeP4Vec( const std::string & name );
        std::vector< double > getMinOfTraces( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        std::vector< double > getMaxOfTraces( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        std::vector< double > getAreaUnderTraces( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        std::vector< double > getTimeStamps( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        std::vector< std::string > getStrTimeStamps( const std::string & name, scopeStructs::SCOPE_PV_TYPE pvType );
        void monitorTracesForNShots( size_t N );
        void monitorNumsForNShots( size_t N );
        std::vector< std::string > getScopeNames();
        /// write a method that returns string version of enums using ENUM_TO_STRING

//        VELA_ENUM::TRIG_STATE getScopeState( const std::string & scopeName );
//        std::string getScopeStateStr( const std::string & name );

        double get_CA_PEND_IO_TIMEOUT();
        void set_CA_PEND_IO_TIMEOUT( double val );

        /// These are pure virtual method in the base class and MUST be overwritten in the derived Controller...
        /// write a method that returns string version of enums using ENUM_TO_STRING

        std::map< VELA_ENUM::ILOCK_NUMBER, VELA_ENUM::ILOCK_STATE > getILockStates( const std::string & name );
        std::map< VELA_ENUM::ILOCK_NUMBER, std::string > getILockStatesStr( const std::string & objName );


    protected:
    private:

        void initialise();

        /// No singletons, no pointers, let's just have an object

        scopeInterface localInterface;

        std::vector< std::string > scopeNames;

        const bool shouldStartEPICS;
        const VELA_ENUM::MACHINE_AREA machineArea;
};


#endif // scopeController_H
