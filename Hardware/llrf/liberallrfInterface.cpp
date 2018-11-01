/*
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
//
//  Author:      DJS
//  Last edit:   13-04-2018
//  FileName:    liberallrfInterface.cpp
//  Description:
//
//
//
//*/
#include "liberallrfInterface.h"
#include "configDefinitions.h"
// stl
#include <iostream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <thread>
#include <math.h>
#include <utility> // std::pair (!)
#include <mutex>   // std::mutex
#include <chrono>   // std::mutex
std::mutex mtx;    // mutex for critical section


#include <boost/circular_buffer.hpp>
//  __  ___  __   __    /  __  ___  __   __
// /  `  |  /  \ |__)  /  |  \  |  /  \ |__)
// \__,  |  \__/ |  \ /   |__/  |  \__/ |  \
//
liberallrfInterface::liberallrfInterface(const std::string &llrfConf,
                              const bool startVirtualMachine,
                              const bool& show_messages,
                              const bool& show_debug_messages_ptr,
                              const bool shouldStartEPICs ,
                              const llrfStructs::LLRF_TYPE type
                             ):
configReader(llrfConf,startVirtualMachine,show_messages,show_debug_messages_ptr),
interface(show_messages,show_debug_messages_ptr),
shouldStartEPICs(shouldStartEPICs),
usingVirtualMachine(startVirtualMachine),
myLLRFType(type),
first_pulse(true),
initial_pulsecount(UTL::ZERO_SIZET),
evid_id(UTL::ZERO_SIZET),
evid_ID_SET(false),
evid_miss_count(-2),
data_miss_count(0),
evid_call_count(0),
data_call_count(0),
EVID_0(-1),
dummy_outside_mask_trace(llrfStructs::outside_mask_trace()),
dummy_trace_data(llrfStructs::rf_trace_data())
{
    /* set the llrf type of the config reader */
    configReader.setType(type);

    initialise();
}
//
// ________                   __
// \______ \   ____   _______/  |________  ____ ___.__.
//  |    |  \_/ __ \ /  ___/\   __\_  __ \/  _ <   |  |
//  |    `   \  ___/ \___ \  |  |  |  | \(   <_>)___  |
// /_______  /\___  >____  > |__|  |__|   \____// ____|
//        \/     \/     \/                     \/
//______________________________________________________________________________
liberallrfInterface::~liberallrfInterface()
{
    killILockMonitors();
    for(auto && it : continuousMonitorStructs)
    {
        killMonitor(it);
        delete it;
    }
    /* kill threads safely */
    for(auto && it = setAmpHP_Threads.cbegin(); it != setAmpHP_Threads.cend() /* not hoisted */; /* no increment */)
    {
        while(!(it->can_kill))
        {

        }
        /// join before deleting...
        /// http://stackoverflow.com/questions/25397874/deleting-stdthread-pointer-raises-exception-libcabi-dylib-terminating
        it->thread->join();
        delete it->thread;
        setAmpHP_Threads.erase(it++);
    }
//    debugMessage("liberallrfInterface DESTRUCTOR COMPLETE ");
}
//______________________________________________________________________________
void liberallrfInterface::killMonitor(llrfStructs::monitorStruct * ms)
{
    int status = ca_clear_subscription(ms->EVID);
    if(status == ECA_NORMAL)
        delete ms;
}
//______________________________________________________________________________
//
//
// .___       .__  __  .__       .__  .__
// |   | ____ |__|/  |_|__|____  |  | |__|_______ ____
// |   |/    \|  \   __\  \__  \ |  | |  \___   // __ \
// |   |   |  \  ||  | |  |/ __ \|  |_|  |/    /\  ___/
// |___|___|  /__||__| |__(____  /____/__/_____ \\___  >
//          \/                 \/              \/    \/
//
// INIT FOLLOWS THE STANDARD DESIGN PATTERN SO IS NOT WELL COMMENTED
// TRACE MONITORS MUST BE STARTED MANUALY AFTER INIT
//______________________________________________________________________________
void liberallrfInterface::initialise()
{
    /* The config file reader read file */
    configFileRead = configReader.readConfig();
    /* why is this here?? */
    //std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // MAGIC_NUMBER
    if(configFileRead)
    {
        message("The liberallrfInterface has read the config file, acquiring objects");
        /// initialise the objects based on what is read from the config file
        bool getDataSuccess = initObjects();
        if(getDataSuccess)
        {
            message("LLRF objects acquired");
            if(shouldStartEPICs)
            {
                message("The liberallrfInterface has acquired objects, connecting to EPICS");
                // subscribe to the channel ids
                initChids();
                // start the monitors: set up the callback functions
                debugMessage("Starting Monitors");
                startMonitors();
                // The pause allows EPICS to catch up.
                standard_pause();
            }
            else
            {
                message("The liberallrfInterface has acquired objects, NOT connecting to EPICS");
            }
        }
        else
        {
            message("!!!The liberallrfInterface received an Error while getting llrf data!!!");
        }
    }
//    debugMessage("liberallrfInterface::initialise() FIN");
}
//______________________________________________________________________________
bool liberallrfInterface::initObjects()
{
    /* get the llrf objects, based on config file */
    bool success = configReader.getliberallrfObject(llrf);
    /* set the TRACE sizes to defaults etc... */
    if(success)
    {
        for(auto&& it:llrf.pvMonStructs)
        {
//            if(it.first == llrfStructs::LLRF_PV_TYPE::ALL_TRACES)
//            {
//                llrf.all_traces.data.resize(it.second.COUNT);
//            }

            if(Is_TracePV(it.first))
            {
                it.second.name = fullCavityTraceName(it.second.name);
                debugMessage("liberallrfInterface, creating trace_data for, ", it.second.name, ", element count = ",it.second.COUNT);
                if(entryExists(llrf.trace_data,it.second.name))
                {
                    message("!!!!ERROR IN TRACES CONFIG FILE DETECTED!!!!!");
                    message("!!!!ABORTING INSTANTIATION!!!!!");
                    return false;
                }
                llrf.trace_data[it.second.name].trace_size = it.second.COUNT;
                llrf.trace_data.at(it.second.name).name = it.second.name;
                success = setIndividualTraceBufferSize(it.second.name, llrf.trace_data.at(it.second.name).buffersize);
                if(!success)
                {
                    message("!!!!ERROR In setIndividualTraceBufferSize DETECTED!!!!! liberallrfInterface");
                    return false;
                }
                llrf.traceLength = it.second.COUNT;
            }
            else if(Is_Time_Vector_PV(it.first))
            {
                llrf.time_vector.resize(it.second.COUNT);
            }
        }




        /*
            Here we are calculating the indices in the ALL-TRACES RECORD, that correpsond to each
            individual trace
        */
        using namespace UTL;
        std::pair<size_t, size_t> temp;
        for(auto i = ZERO_SIZET; i < llrf.all_traces.num_traces; ++i)
        {
            temp.first  = i * llrf.all_traces.num_elements_per_trace + llrf.all_traces.num_start_zeroes_per_trace;
            temp.second = (i + ONE_SIZET) * llrf.all_traces.num_elements_per_trace - ONE_SIZET;
            llrf.all_traces.trace_indices.push_back(temp);

            //message("Trace ", i , ": ",temp.first , " - ", temp.second );
        }
        /*
            Here we print out where we think each trace is in the ONE RECORD,
            and what its indicies are in the ONE RECORD
        */
        message("\nPrinting Offline 'One Record' Trace Information\n");
        for(auto&& it: llrf.all_traces.trace_info)
        {
            message(it.second.name," is a ", ENUM_TO_STRING(it.second.type)," trace, position = ",it.second.position,
                    ", indices = ",
                    llrf.all_traces.trace_indices[it.second.position].first,
                    " - ",
                    llrf.all_traces.trace_indices[it.second.position].second);
        }
        /*
            Initialise the Trace Data buffers

            First the individual trace buffers, these are held in the top level llrfobject
            we use llrf.all_traces.trace_info to set these

            Second the AllTrace Buffer, (this is genreally much larger so we can dump more data)
        */
        for(auto&& it: llrf.all_traces.trace_info)
        {
            llrf.trace_data_2[it.second.name].name = it.second.name;
            setIndividualTraceBufferSize( it.second.name, llrf.trace_data_2.at(it.second.name).buffersize );
        }
        setAllTraceBufferSize(llrf.all_traces.buffer_size);




//        std::pair<int, std::string> temp3;
//        temp3.first  = UTL::ZERO_INT;
//        temp3.second = UTL::START_UP;
        //llrf.all_traces.EVID_buffer.assign( llrf.all_traces.buffer_size, temp2);

        //message("llrf.all_traces.EVID_buffer.size() = ", llrf.all_traces.EVID_buffer.size());


        /*


        */




//
//        for(size_t i; i =0; i < llrf.all_traces.buffer_size, ++i)
//        {
//            llrf.all_traces.EVID_buffer.push_back();
//        }
//        for(size_t i; i =0; i < llrf.all_traces.buffer_size, ++i)
//        {
//            llrf.all_traces.data_buffer.push_back();
//            llrf.all_traces.data_buffer.back().first  = "START_UP";
//            llrf.all_traces.data_buffer.back().second.resize(
//
//            (size_t)llrf.all_traces.num_traces * llrf.all_traces.num_elements_per_trace
//                                                            );
//        }
//        message("llrf.all_traces.EVID_buffer.size() = ", llrf.all_traces.EVID_buffer.size());
//        message("llrf.all_traces.data_buffer.size() = ", llrf.all_traces.data_buffer.size());





    }
    llrf.type = myLLRFType;
    return success;
}
//______________________________________________________________________________
void liberallrfInterface::initChids()
{
    message("\n", "liberallrfInterface is searching for LLRF ChIds...");
    for(auto && it:llrf.pvMonStructs)
    {
        addChannel(llrf.pvRoot, it.second);
    }
    // command only PVs for the LLRF to set "high level" phase and amplitide
    for(auto && it:llrf.pvComStructs)
    {
        addChannel(llrf.pvRoot, it.second);
    }
    for(auto && it:llrf.pvOneTraceMonStructs)
    {
        addChannel(llrf.pvRoot + llrf.pvRoot_oneTrace, it.second);
    }
    for(auto && it:llrf.pvOneTraceComStructs)
    {
        addChannel(llrf.pvRoot + llrf.pvRoot_oneTrace, it.second);
    }

    //for the scans i have to loop over eahc SCN object and then include th emain pvroot, the scn apvroot then th epvsuffix

    for(auto && it:llrf.trace_scans)
    {
        for(auto && it2: it.second.pvSCANStructs)
        {
            addChannel(llrf.pvRoot + it.second.pvroot, it2.second);
        }
        /*
            For SCAN the monitor is the readbac and the command
        */
//        for(auto && it:llrf.pvSCANComStructs)
//        {
//            addChannel(llrf.pvRoot + it1.second.pvroot, it.second);
//        }
    }

    //addILockChannels(llrf.numIlocks, llrf.pvRoot, llrf.name, llrf.iLockPVStructs);
    int status=sendToEpics("ca_create_channel","liberallrfInterface found LLRF ChIds.",
                           "!!TIMEOUT!! Not all LLRF ChIds found, liberallrfInterface.");
    /* if timeout try investigating */
    if(status==ECA_TIMEOUT)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));//MAGIC_NUMBER
        message("\n","liberallrfInterface will check the  LLRF ChIds ");
        for(auto && it : llrf.pvMonStructs)
        {
            checkCHIDState(it.second.CHID, ENUM_TO_STRING(it.first));
        }
        for(auto && it : llrf.pvComStructs)
        {
            checkCHIDState(it.second.CHID, ENUM_TO_STRING(it.first));
        }
        /* SCANS only have one flavour */

        for(auto &&it:llrf.trace_scans)
        {
            message("CHECK CHIDS FOR ", it.first);
            for(auto && it2: it.second.pvSCANStructs)
            {
                checkCHIDState(it2.second.CHID, ENUM_TO_STRING(it2.first));
            }
        }

        for(auto && it : llrf.pvOneTraceMonStructs)
        {
            checkCHIDState(it.second.CHID, ENUM_TO_STRING(it.first));
        }
        for(auto && it : llrf.pvOneTraceComStructs)
        {
            checkCHIDState(it.second.CHID, ENUM_TO_STRING(it.first));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // MAGIC_NUMBER
    }
    else if (status == ECA_NORMAL)
    {
        allChidsInitialised = true;  // interface base class member
    }
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::addChannel(const std::string & pvRoot, llrfStructs::pvStruct& pv)
{
    std::string s;
    if(pv.pvType == llrfStructs::LLRF_PV_TYPE::AMP_MVM)
    {
        s =  UTL::VM_PREFIX + pv.pvSuffix;
    }
    else if(pv.pvType == llrfStructs::LLRF_PV_TYPE::PHI_DEG)
    {
        s =  UTL::VM_PREFIX + pv.pvSuffix;
    }
    else
    {
        s = pvRoot + pv.pvSuffix;
    }
    ca_create_channel(s.c_str(), nullptr, nullptr, UTL::PRIORITY_99, &pv.CHID);
    debugMessage("Create channel to ", s);
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::startMonitors()
{
    continuousMonitorStructs.clear();
    continuousILockMonitorStructs.clear();

    for(auto&& it:llrf.pvMonStructs)
    {
        addTo_continuousMonitorStructs(it);
    }
    for(auto&& it:llrf.pvOneTraceMonStructs)
    {
        addTo_continuousMonitorStructs(it);
    }
    for(auto&& it:llrf.trace_scans)
    {
        //debugMessage("creating subscriptions to ", it.first);
        for(auto&& it2: it.second.pvSCANStructs)
        {
            addTo_continuousMonitorStructs(it2);
            continuousMonitorStructs.back()->name = it.first;
//            message("Using ", continuousMonitorStructs.back() -> name,
//                    " and ",  ENUM_TO_STRING(continuousMonitorStructs.back() -> monType));
        }
    }

    int status = sendToEpics("ca_create_subscription",
                             "liberallrfInterface succesfully subscribed to LLRF monitors.",
                             "!!TIMEOUT!! Subscription to LLRF monitors failed, liberallrfInterface");
    if (status == ECA_NORMAL)
    {
        allMonitorsStarted = true; // interface base class member
    }

//    LOOP OVER ALL CONTINUOUS MONITOR STRUCTS TO GET names debugging
//    for(auto&&it:continuousMonitorStructs)
//    {
//        message(it->name," with ", ENUM_TO_STRING(it -> monType) );
//    }

}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::addTo_continuousMonitorStructs(const std::pair<llrfStructs::LLRF_PV_TYPE,llrfStructs::pvStruct>& it)
{
    debugMessage("ca_create_subscription to ", ENUM_TO_STRING(it.first));
    continuousMonitorStructs.push_back(new llrfStructs::monitorStruct());
    continuousMonitorStructs.back() -> monType   = it.first;
    continuousMonitorStructs.back() -> llrfObj   = &llrf;
    continuousMonitorStructs.back() -> interface = this;
    continuousMonitorStructs.back() -> name      = ENUM_TO_STRING(llrf.type);
    ca_create_subscription(it.second.CHTYPE,
                           it.second.COUNT,
                           it.second.CHID,
                           it.second.MASK,
                           liberallrfInterface::staticEntryLLRFMonitor,
                           (void*)continuousMonitorStructs.back(),
                           &continuousMonitorStructs.back() -> EVID);
}
//--------------------------------------------------------------------------------------------------
//
// _________        .__  .__ ___.                  __
// \_   ___ \_____  |  | |  |\_ |__ _____    ____ |  | __
// /    \  \/\__  \ |  | |  | | __ \\a__  \ _/ ___\|  |/ /
// \     \____/ __ \|  |_|  |_| \_\ \/ __ \\  \___|    <
//  \______  (____  /____/____/___  (____  /\___  >__|_ \
//         \/     \/              \/     \/     \/     \/
//
// These functions are called from EPICS subscriptions
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::staticEntryLLRFMonitor(const event_handler_args args)
{
    llrfStructs::monitorStruct*ms = static_cast<llrfStructs::monitorStruct *>(args.usr);
    ms->interface->updateLLRFValue(ms->monType, ms->name, args);
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateLLRFValue(const llrfStructs::LLRF_PV_TYPE pv, const std::string& objName,
                                          const event_handler_args& args)
{
    //message("callback ", ENUM_TO_STRING(pv), " ", objName );
    using namespace llrfStructs;
    switch(pv)
    {
        case LLRF_PV_TYPE::TRIG_SOURCE:
            updateTrigState(args);
            break;
//        case LLRF_PV_TYPE::LIB_ILOCK_STATE:
//            updateBoolState(args, llrf.interlock_state);
//            break;
        case LLRF_PV_TYPE::LIB_FF_AMP_LOCK_STATE:
            updateBoolState(args, llrf.ff_amp_lock_state);
            break;
        case LLRF_PV_TYPE::LIB_FF_PHASE_LOCK_STATE:
            updateBoolState(args, llrf.ff_ph_lock_state);
            break;
        case LLRF_PV_TYPE::LIB_RF_OUTPUT:
            updateBoolState(args, llrf.rf_output);
            break;
        case LLRF_PV_TYPE::LIB_AMP_FF:
            //ms->interface->debugMessage("staticEntryLLRFMonitor LIB_AMP_FF = ",*(double*)args.dbr);
            llrf.amp_ff = *(double*)args.dbr;
            llrf.amp_MVM = (llrf.amp_ff) * (llrf.ampCalibration);
            break;
        case LLRF_PV_TYPE::LIB_AMP_SP:
            //ms->interface->debugMessage("staticEntryLLRFMonitor LIB_AMP_SP = ",*(double*)args.dbr);
            llrf.amp_sp = *(double*)args.dbr;
            //(ms->llrfObj->amp_sp)
            llrf.amp_MVM = (llrf.amp_sp) * (llrf.ampCalibration);
            break;
        case LLRF_PV_TYPE::LIB_PHI_FF:
            //ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PHI_FF = ",*(double*)args.dbr);
            llrf.phi_ff = *(double*)args.dbr;
            llrf.phi_DEG = (llrf.phi_ff) * (llrf.phiCalibration);
            break;
        case LLRF_PV_TYPE::LIB_PHI_SP:
            //ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PHI_SP = ",*(double*)args.dbr);
            llrf.phi_sp = *(double*)args.dbr;
            llrf.phi_DEG = (llrf.phi_sp) * (llrf.phiCalibration);
            break;
        case LLRF_PV_TYPE::LIB_TIME_VECTOR:
            /* the time_vector is simlar to a trace, but we can just update the values straight away */
            message(ENUM_TO_STRING(LLRF_PV_TYPE::LIB_TIME_VECTOR));
            updateTimeVector(args);
            break;
        case LLRF_PV_TYPE::LIB_PULSE_LENGTH:
            llrf.pulse_length = *(double*)args.dbr;
            break;
        case LLRF_PV_TYPE::LIB_PULSE_OFFSET:
            //ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PULSE_OFFSET = ",*(double*)args.dbr);
            llrf.pulse_offset = *(double*)args.dbr;
            break;

        case LLRF_PV_TYPE::ALL_TRACES:
             updateAllTraces(args);
             break;
        case LLRF_PV_TYPE::ALL_TRACES_SCAN:
             message("update ALL_TRACES_SCAN");
             break;

        case LLRF_PV_TYPE::ALL_TRACES_ACQM:
             message("update ALL_TRACES_ACQM");
             break;
        case LLRF_PV_TYPE::ALL_TRACES_EVID:
            updateAllTracesEVID(args);
            break;

        case LLRF_PV_TYPE::LIB_PWR_REM_SCAN:
            updateSCAN(objName, pv, args);
            break;
        case LLRF_PV_TYPE::LIB_PHASE_REM_SCAN:
            updateSCAN(objName, pv, args);
            break;
        case LLRF_PV_TYPE::LIB_AMP_DER_SCAN:
            updateSCAN(objName, pv, args);
            break;
        case LLRF_PV_TYPE::LIB_PHASE_DER_SCAN:
            updateSCAN(objName, pv, args);
            break;
        case LLRF_PV_TYPE::LIB_PWR_LOCAL_SCAN:
            updateSCAN(objName, pv, args);
            break;
        case LLRF_PV_TYPE::INTERLOCK:
            switch(getDBRunsignedShort(args))
            {
                case 0: llrf.interlock_state = llrfStructs::INTERLOCK_STATE::NON_ACTIVE; break;
                case 1: llrf.interlock_state = llrfStructs::INTERLOCK_STATE::ACTIVE; break;
                default: llrf.interlock_state = llrfStructs::INTERLOCK_STATE::NON_ACTIVE;
            }
            break;
        default:
            debugMessage("ERROR updateLLRFValue passed Unknown PV Type = ", ENUM_TO_STRING(pv));
    }
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateTimeVector(const event_handler_args& args)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const dbr_double_t* p_data = (const dbr_double_t*)args.dbr;
    std::copy(p_data, p_data + llrf.time_vector.size(), llrf.time_vector.begin());
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateSCAN(const std::string& name, const llrfStructs::LLRF_PV_TYPE pv, const event_handler_args& args)
{
    //message("updateSCAN called ", name, " ", ENUM_TO_STRING(pv) );
    llrfStructs::LLRF_SCAN new_val = llrfStructs::LLRF_SCAN::UNKNOWN_SCAN;
    switch( getDBRunsignedLong(args) )
    {
        case 0:
            new_val = llrfStructs::LLRF_SCAN::PASSIVE;
            break;
        case 1:
            new_val = llrfStructs::LLRF_SCAN::EVENT;
            break;
        case 2:
            new_val = llrfStructs::LLRF_SCAN::IO_INTR;
            break;
        case 3:
            new_val = llrfStructs::LLRF_SCAN::TEN;
            break;
        case 4:
            new_val = llrfStructs::LLRF_SCAN::FIVE;
            break;
        case 5:
            new_val = llrfStructs::LLRF_SCAN::TWO;
            break;
        case 6:
            new_val = llrfStructs::LLRF_SCAN::ONE;
            break;
        case 7:
            new_val = llrfStructs::LLRF_SCAN::ZERO_POINT_FIVE;
            break;
        case 8:
            new_val = llrfStructs::LLRF_SCAN::ZERO_POINT_TWO;
            break;
        case 9:
            new_val = llrfStructs::LLRF_SCAN::ZERO_POINT_ONE;
            break;
        case 10:
            new_val = llrfStructs::LLRF_SCAN::ZERO_POINT_ZERO_FIVE;
            break;
    }
    if(entryExists(llrf.trace_scans, name))
    {
        if(entryExists(llrf.trace_scans.at(name).value,pv))
        {

        }
        else
        {
            message(pv, " is not in trace_scans(",name,")");
        }
    }
    else
    {
        // then we assume it is the ONE

        message(name, " is not in trace_scans");
    }
    llrf.trace_scans.at(name).value.at(pv) = new_val;
    //message(name, " ", ENUM_TO_STRING(pv), " = ", ENUM_TO_STRING(llrf.trace_scans.at(name).value.at(pv))  );
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateAllTraces(const event_handler_args& args)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    /*
        time how long this function takes to run
    */
    std::chrono::high_resolution_clock::time_point start1 = std::chrono::high_resolution_clock::now();
    /*
        Pointer to the data + timestamp
    */

    /* push back temp data to all_traces.data_buffer */
    llrf.all_traces.data_buffer.push_back( temp_all_trace_item );

    const dbr_time_double* p_data = (const struct dbr_time_double*)args.dbr;
    /*
        Get timestamp and add to data_buffer
    */

    llrf.all_traces.data_buffer.back().first = p_data->stamp;
    if(llrf.all_traces.trace_counter == UTL::ZERO_SIZET)
    {
        llrf.all_traces.trace_time_0 = llrf.all_traces.data_buffer.back().first;
    }
    /*
        Get the data and add to data_buffer, this assumes the data is 'new'
        there is a way to set up the LLRF such that it sends the same data multiple times
    */
    const dbr_double_t * pValue;
    pValue = &p_data->value;


    std::copy(pValue, pValue + llrf.all_traces.data_buffer.back().second.size(),
                  llrf.all_traces.data_buffer.back().second.begin());
    /*
        Update counters
    */
    data_call_count += UTL::ONE_INT;
    llrf.all_traces.trace_counter += UTL::ONE_SIZET;
    /*
        copy data to individual trace buffers
    */
    for(auto&& it: llrf.all_traces.trace_info)
    {
        /* push_back temp data */
        llrf.trace_data_2.at(it.first).data_buffer.push_back( temp_trace_item );


        /* copy timestamp to trace data_buffer*/
        llrf.trace_data_2.at(it.first).data_buffer.back().first = llrf.all_traces.data_buffer.back().first;
        /* copy data to trace data_buffer */
        auto data_start = llrf.all_traces.data_buffer.back().second.begin() + llrf.all_traces.trace_indices.at(it.second.position).first;
        auto data_end   = data_start + llrf.all_traces.num_elements_per_trace_used;
        std::copy( data_start, data_end, llrf.trace_data_2.at(it.first).data_buffer.back().second.begin() );
        /*
            If this is a power trace then square it and divide by 100
        */
        if( it.second.type == llrfStructs::TRACE_TYPE::POWER)
        {
            for( auto &v : llrf.trace_data_2.at(it.first).data_buffer.back().second )
            {
                v *= v;
                v /= UTL::ONEHUNDRED_DOUBLE;
            }
        }
        else if(  it.second.type == llrfStructs::TRACE_TYPE::PHASE )
        {
            //unwrapPhaseTrace(llrf.trace_data_2.at(it.first));
        }
    }

    // NOW WE FOLLOW THE PROCEDURE

    // if collecting futre traces, collect and  increment counter
    checkCollectingFutureTraces();


    // CHECK_KLYSTRON_FWD_PWR for active pulse
    llrf.kly_fwd_power_max = getKlyFwdPwrMax();
    updateActivePulses( );


    if(llrf.can_increase_active_pulses)
    {
        // check for breakdowns, if outside_mask_deteced switch off RF
        checkForOutsideMaskTrace();

        // update masks
        updateMasks();

    }
    /*
        bench marking
    */
    std::chrono::high_resolution_clock::time_point end1 = std::chrono::high_resolution_clock::now();
    data_time_RS.Push( std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1).count() );
    size_t max_count = 1000;
    if( llrf.all_traces.trace_counter % max_count == UTL::ZERO_SIZET )
    {
        /*
            get the time as double for trace_time_0
        */
        std::string time_str;
        double time_num_0;
        updateTime(llrf.all_traces.trace_time_0,
                   time_num_0,
                   time_str);


        double time_num;
        updateTime(llrf.all_traces.data_buffer.back().first,
                   time_num,
                   time_str);
        /*
            get rep rate in last 6000 traces
        */
        double dt = time_num - time_num_0;
        double freq = max_count / dt;

        message(time_str, " count ", max_count, "/", llrf.all_traces.trace_counter, " took ,", dt," s, <process_time> = ", data_time_RS.Mean(), " us, DAQ freq = ", freq, " Hz, active_pulse_count ", llrf.active_pulse_count );

        data_time_RS.Clear();
        llrf.all_traces.trace_time_0 = llrf.all_traces.data_buffer.back().first;
    }
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setUnwrapPhaseTolerance(const double value)
{
    bool r = true;
    for(auto&&it:llrf.trace_data_2)
    {
        bool r2 = setUnwrapPhaseTolerance(it.first,value);
        if( r2 == false)
        {
            r = false;
        }
    }
    return r;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setUnwrapPhaseTolerance(const std::string& name,const double value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).phase_tolerance = value;
        return true;
    }
    message("liberallrfInterface::setMaskValue ERROR, trace ", n, " does not exist");
    return false;
}

//--------------------------------------------------------------------------------------------------
void liberallrfInterface::unwrapPhaseTrace(llrfStructs::rf_trace_data_new& trace_data)
{
/*
    Mathematica function to do this, based on a tolerance of phase jumps ...
    Unwrap[lst_List,D_,tolerance_]:= Module[{tol,jumps}, tol=If[Head[tolerance]===Scaled,\[CapitalDelta] tolerance[[1]],tolerance];

    (*Finds differences between subsequent points*)
    jumps=Differences[lst];
    (* Finds where difference is larger than a given tolerance and sets these numbers to +- 1 depending on sign of  difference and all others to zero*)
    jumps=-Sign[jumps]Unitize[Chop[Abs[jumps],tol]];
    (* Makes list of successive accumulated total of our 1s & -1s ie. history of how many jumps have been made,
       adds a 0 to the beginning to make up for the one lost calculating differences, then multiplies by 360  *)
    jumps=D Prepend[Accumulate[jumps],0];
    (*adds our multiples of 360 to the original phase*)
    jumps+lst
    ]

*/
    // Things TO DO
    // only unwrap numbers between mask start and end ...
    // make faster , maybe all inside one loop

    std::vector<double>& phase_trace = trace_data.data_buffer.back().second;

    double jump_sum = 0.0;
    for(auto i = 0; i < phase_trace.size() - 1; ++i )
    {
        if( i < trace_data.mask_start )
        {

        }
        else if( trace_data.mask_start <= i && i < trace_data.mask_end )
        {
            double jump = phase_trace[i + 1] - phase_trace[i];

            double t = fabs( jump );
            if(t < trace_data.phase_tolerance )//MAGIC_NUMBER
            {
                t = UTL::ZERO_DOUBLE;
            }
            else
            {
                t = UTL::ONE_DOUBLE;
            }
            jump = -(double)sgn(jump) * t;

            jump_sum += jump;

            //j *= 180.0;//MAGIC_NUMBER

            if( i < trace_data.mask_start )
            {

            }
            phase_trace[i] += jump_sum * trace_data.phase_tolerance;
        }
        else
        {
            phase_trace[i] += jump_sum * trace_data.phase_tolerance;
        }

    }
    phase_trace.back() +=  jump_sum * trace_data.phase_tolerance;


//
////    for(auto& it: phase_trace)
////    {
////        std::cout << it << " ";
////    }
////    std::cout << std::endl;
////    std::cout << std::endl;
//
//    std::vector<double> jumps(phase_trace.size()-1, UTL::ZERO_DOUBLE);
//
//
//    // std::adjacent_difference c++17
//    // std::adjacent_difference(phase_trace.begin(), phase_trace.end(), jumps.begin());
//    //
//    for(auto i = 0; i < jumps.size(); ++i )
//    {
//        jumps[i] = phase_trace[i+1] - phase_trace[i];
//        //std::cout << jumps[i] << " ";
//    }
////    std::cout << std::endl;
////    std::cout << std::endl;
//
//
////    for(auto&  data_it = phase_trace.begin(), jumps_it = jumps.begin();
////               data_it < phase_trace.end() && jumps_it < jumps.end();
////               ++data_it , ++jumps_it )
////    {
////            *jumps_it = *(data_it + 1) - *data_it;
////            std::cout << *jumps_it << " ";
////    }
////    std::cout << std::endl;
////    std::cout << std::endl;
//
//    for(auto& it:jumps)
//    {
//        //jumps=-Sign[jumps]Unitize[Chop[Abs[jumps],tol]];
//
//
//        //std::cout << it << " ";
//
//    }
////    std::cout << std::endl;
////    std::cout << std::endl;
//
//
//    std::partial_sum(jumps.begin(), jumps.end(), jumps.begin(), std::plus<double>());
//
////    for(auto& it: jumps)
////    {
////        std::cout << it << " ";
////    }
////    std::cout << std::endl;
////    std::cout << std::endl;
//
//
//    for(auto& j: jumps)
//    {
//        j *= 180.0;//MAGIC_NUMBER
//    }
//    jumps.insert( jumps.begin(), 0.0 ); //MAGIC_NUMBER
//
//
//    for(auto&& data_it = phase_trace.begin(), jump_it = jumps.begin();
//               data_it < phase_trace.end() && jump_it < jumps.end();
//               ++data_it , ++jump_it )
//    {
//        *data_it += *jump_it;
////        std::cout << *data_it << " ";
//    }
////    std::cout << std::endl;
////    std::cout << std::endl;
//   // message(data.name, " unwrapped phase ");
}

//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateMasks()
{
    for(auto&&it:llrf.trace_data_2)
    {
        updateTraceRollingAverage(it.second);
        /*
            we probably want to check that the gloabl check mask is true here,
            we DONT want to update masks when the RF has been switched off.
        */
        if( it.second.check_mask && it.second.has_average )
        {
            //message("UPDATE MASK ", it.first);
            setNewMask(it.second);
        }
    }
}
//--------------------------------------------------------------------------------------------------
// THIS FUNCTION ACTUALLY SETS THE MASK VALUES BASED ON THE ROLLING AVERAGE AND
// THE USER SEPCIFIED MASK VALUES
void liberallrfInterface::setNewMask(llrfStructs::rf_trace_data_new& data)
{
    // this function assumes everything is good to set a new mask
    // automatically set the mask based on the rolling_average
    //
    // 1) element 0                   and mask_start        set to default hi/lo (+/-inf)
    // 2) element mask_start+1        and mask_window_start set by rolling_average +/- mask_value
    // 3) element mask_window_start+1 and mask_window_end   set very default hi/lo (+/-inf)
    // 4) element mask_window_end+1   and mask_end          set by rolling_average +/- mask_value
    // 5) element mask_end+1          and end               set very default hi/lo (+/-inf)

    /* ref to the latest rolling average, this is used to defen the mask */
    std::vector<double>& ra = data.rolling_average;

    double hi_max =  std::numeric_limits<double>::infinity();
    double lo_min = -std::numeric_limits<double>::infinity();
    //
    data.hi_mask.resize(ra.size(), UTL::ZERO_DOUBLE);
    data.lo_mask.resize(ra.size(), UTL::ZERO_DOUBLE);
    /* ref to hi adn lo masks */
    std::vector<double>& hi_mask = data.hi_mask;
    std::vector<double>& lo_mask = data.lo_mask;
    //
    // mask_value is what to add or subtrace from the
    // rolling_average to set the hi/lo mask
    double mask_value = UTL::ZERO_DOUBLE;
    std::string mask_type;
    if(data.use_percent_mask)
    {
        mask_type = "setPercentMask";
    }
    else if(data.use_abs_mask)
    {
        mask_type = "setAbsoluteMask";
        mask_value = data.mask_value;
    }
    //
    // MASK part 1
    for(auto i = UTL::ZERO_SIZET; i <= data.mask_start; ++i)
    {
        hi_mask[i] = hi_max;
        lo_mask[i] = lo_min;
    }
    //
    // MASK part 2
    for(auto i = data.mask_start + UTL::ONE_SIZET; i <= data.mask_end; ++i)
    {
        if(data.use_percent_mask)
        {
            mask_value = fabs(ra[i] * data.mask_value);
            if( mask_value < data.mask_abs_min )
            {
                mask_value = data.mask_abs_min;
            }
        }
        hi_mask[i] = ra[i] + mask_value;
        lo_mask[i] = ra[i] - mask_value;
    }
    //
    // MASK part 3
    for(auto i = data.mask_window_start + UTL::ONE_SIZET; i <= data.mask_window_end; ++i)
    {
        hi_mask[i] = hi_max;
        lo_mask[i] = lo_min;
    }
    //
    // MASK part 4
//    for(auto i = data.mask_window_end + UTL::ONE_SIZET; i <= data.mask_end; ++i)
//    {
//        if(data.use_percent_mask)
//        {
//            mask_value = fabs(ra[i] * data.mask_value);
//            if( mask_value < data.mask_abs_min )
//            {
//                mask_value = data.mask_abs_min;
//            }
//        }
//        hi_mask[i] = ra[i] + mask_value;
//        lo_mask[i] = ra[i] - mask_value;
//    }
    //
    // MASK part 5
    for(auto i = data.mask_end + UTL::ONE_SIZET; i < ra.size(); ++i)
    {
        hi_mask[i] = hi_max;
        lo_mask[i] = lo_min;
    }
    data.lo_mask_set = true;
    data.hi_mask_set = true;
}
//--------------------------------------------------------------------------------------------------
/*             __           __   ___ ___ ___  ___  __   __
    |\/|  /\  /__` |__/    /__` |__   |   |  |__  |__) /__`
    |  | /~~\ .__/ |  \    .__/ |___  |   |  |___ |  \ .__/

    // set the basic paramters for a mask, its type, ( % or Abs)
    // the value
    // start end, floor and window

*/
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
// There is a high level, global check mask funciton, this can be set from various places
// to enable / disable checking masks
//
void liberallrfInterface::setGlobalCheckMask(bool value)
{
    llrf.check_mask = value;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setGlobalShouldCheckMask()
{
    setGlobalCheckMask(true);
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setGlobalShouldNotCheckMask()
{
    setGlobalCheckMask(false);
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setShouldCheckMask(const std::string&name)
{
    return setCheckMask(name, true);
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setShouldNotCheckMask(const std::string&name)
{
    return setCheckMask(name, false);
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setCheckMask(const std::string&name, bool value)
{
    //std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).check_mask = value;
        setKeepRollingAverage(n,value);
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setUsePercentMask(const std::string& name)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).use_percent_mask  = true;
        llrf.trace_data_2.at(n).use_abs_mask = false;
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setUseAbsoluteMask(const std::string& name)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).use_percent_mask  = false;
        llrf.trace_data_2.at(n).use_abs_mask = true;
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskAbsMinValue(const std::string& name,const double value)
{
    if(value < UTL::ZERO_DOUBLE)
    {
        message("liberallrfInterface::setMaskValue ERROR, mask_value can not be < 0");
        return false;
    }
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).mask_abs_min = value;
        return true;
    }
    message("liberallrfInterface::setMaskValue ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskValue(const std::string& name,const double value)
{
    if(value < UTL::ZERO_DOUBLE)
    {
        message("liberallrfInterface::setMaskValue ERROR, mask_value can not be < 0");
        return false;
    }
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).mask_value  = value;
        return true;
    }
    message("liberallrfInterface::setMaskValue ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskStartIndex(const std::string& name, size_t value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).mask_start = value;
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskEndIndex(const std::string& name, size_t value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).mask_end = value;
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskFloor(const std::string& name, double value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).mask_floor = value;
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskWindowIndices(const std::string& name, size_t window_start, size_t window_end )
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).mask_window_start = window_start;
        llrf.trace_data_2.at(n).mask_window_end   = window_end;
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskStartTime(const std::string& name, double value)
{
    return setMaskStartIndex(name, getIndex(value));
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskEndTime(const std::string& name, double value)
{
    return setMaskEndIndex(name, getIndex(value));
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskWindowTimes(const std::string& name, double window_start, double window_end)
{
    return setMaskWindowIndices(name, getIndex(window_start), getIndex(window_end));
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskParamatersIndices(const std::string& name, bool isPercent,
                                                   double mask_value, double mask_floor, double mask_abs_min,
                                                   size_t start, size_t end,
                                                   size_t window_start, size_t window_end
                                                   )
{
    // sanity check
    if(UTL::ZERO_SIZET <= start && end <= llrf.all_traces.num_elements_per_trace_used)
    {
        if(start <= end && window_start <= window_end )
        {
            bool r = false;
            if(isPercent)
            {
                r = setUsePercentMask(name);
            }
            else
            {
                r = setUseAbsoluteMask(name);
            }
            if(r)
            {
                if( setMaskValue(name, mask_value) )
                {
                    if(setMaskStartIndex(name , start))
                    {
                        if( setMaskEndIndex(name, end))
                        {
                            if(setMaskWindowIndices(name, window_start, window_end))
                            {
                                if( setMaskAbsMinValue(name, mask_abs_min))
                                {
                                    if( setMaskFloor(name, mask_floor))
                                    {
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setMaskParamatersTimes(const std::string& name, bool isPercent,
                                                 double mask_value, double mask_floor, double mask_abs_min,
                                                 double start, double end,
                                                 double window_start, double window_end
                                                   )
{
    return setMaskParamatersIndices(name, isPercent, mask_value, mask_floor, mask_abs_min,
                                    getIndex(start), getIndex(end),
                                    getIndex(window_start),getIndex(window_end));
}
//--------------------------------------------------------------------------------------------------
/*             __           __   ___ ___ ___  ___  __   __
    |\/|  /\  /__` |__/    / _` |__   |   |  |__  |__) /__`
    |  | /~~\ .__/ |  \    \__> |___  |   |  |___ |  \ .__/
    //
*/
std::vector<double> liberallrfInterface::getHiMask(const std::string& name)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        if(llrf.trace_data_2.at(name).hi_mask_set)
        {
            return llrf.trace_data_2.at(n).hi_mask;
        }
    }
    message("liberallrfInterface::getHiMask ERROR, trace ", n, " does not exist");
    std::vector<double> r{UTL::DUMMY_DOUBLE};//MAGIC_NUMBER
    return r;
}
//--------------------------------------------------------------------------------------------------
std::vector<double> liberallrfInterface::getLoMask(const std::string& name)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        if(llrf.trace_data_2.at(n).lo_mask_set)
        {
            return llrf.trace_data_2.at(n).lo_mask;
        }
    }
    message("liberallrfInterface::getLoMask ERROR, trace ", n, " does not exist");
    std::vector<double> r{UTL::DUMMY_DOUBLE};//MAGIC_NUMBER
    return r;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getMaskValue(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).mask_value;
    }
    message("liberallrfInterface::getMaskValue ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getMaskStartIndex(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).mask_start;
    }
    message("liberallrfInterface::getMaskStartIndex ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getMaskEndIndex(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).mask_end;
    }
    message("liberallrfInterface::getMaskEndIndex ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getMaskFloor(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).mask_floor;
    }
    message("liberallrfInterface::getMaskFloor ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getMaskWindowStartIndex(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).mask_window_start;
    }
    message("liberallrfInterface::getMaskWindowStartIndex ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getMaskWindowEndIndex(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).mask_window_end;
    }
    message("liberallrfInterface::getMaskWindowEndIndex ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getMaskStartTime(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return getTime(llrf.trace_data_2.at(n).mask_start);
    }
    message("liberallrfInterface::getMaskStartTime ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getMaskEndTime(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return getTime(llrf.trace_data_2.at(n).mask_end);
    }
    message("liberallrfInterface::getMaskEndTime ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getMaskWindowStartTime(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return getTime(llrf.trace_data_2.at(n).mask_window_start);
    }
    message("liberallrfInterface::getMaskWindowStartTime ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getMaskWindowEndTime(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return getTime(llrf.trace_data_2.at(n).mask_window_end);
    }
    message("liberallrfInterface::getMaskWindowEndTime ERROR, trace ", n, " does not exist");
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------





//
//
// MASKS
//____________________________________________________________________________________________
//____________________________________________________________________________________________
bool liberallrfInterface::isCheckingMask(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2,n))
        return llrf.trace_data_2.at(n).check_mask;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::isNotCheckingMask(const llrfStructs::LLRF_PV_TYPE pv)
{
    return isCheckingMask(getLLRFChannelName(pv));
}
//____________________________________________________________________________________________
bool liberallrfInterface::isNotCheckingMask(const std::string& name)
{
    return !isCheckingMask(name);
}
//____________________________________________________________________________________________
bool liberallrfInterface::isCheckingMask(const llrfStructs::LLRF_PV_TYPE pv)
{
    return !isCheckingMask(pv);
}
//____________________________________________________________________________________________
bool liberallrfInterface::clearMask(const std::string&name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).hi_mask.clear();
        llrf.trace_data_2.at(n).lo_mask.clear();
        llrf.trace_data_2.at(n).hi_mask_set  = false;
        llrf.trace_data_2.at(n).lo_mask_set = false;
        return true;
    }
    message("liberallrfInterface::clearMask ERROR, trace ", n, " does not exist");
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setInfiniteMasks(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        std::vector<double> p_inf(llrf.trace_data_2.at(n).trace_size,  std::numeric_limits<double>::infinity());
        std::vector<double> n_inf(llrf.trace_data_2.at(n).trace_size, -std::numeric_limits<double>::infinity());
        llrf.trace_data_2.at(n).hi_mask = p_inf;
        llrf.trace_data_2.at(n).lo_mask = n_inf;
        llrf.trace_data_2.at(n).hi_mask_set = true;
        llrf.trace_data_2.at(n).lo_mask_set = true;
        return true;
    }
    message("liberallrfInterface::setInfiniteMasks ERROR, trace ", n, " does not exist");
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setHiMask(const std::string&name,const std::vector<double>& value)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        if(value.size() == llrf.trace_data_2.at(n).trace_size)
        {
            llrf.trace_data_2.at(n).hi_mask = value;
            llrf.trace_data_2.at(n).hi_mask_set = true;
            return true;
        }
    }
    message("liberallrfInterface::setHiMask ERROR, trace ", n, " does not exist");
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setLoMask(const std::string&name,const std::vector<double>& value)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        if(value.size() == llrf.trace_data_2.at(n).trace_size)
        {
            llrf.trace_data_2.at(n).lo_mask = value;
            llrf.trace_data_2.at(n).lo_mask_set = true;
            return true;
        }
    }
    message("liberallrfInterface::setLoMask ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setNumContinuousOutsideMaskCount(size_t value)
{
    bool r = true;
    bool temp;
    for(auto&& it: llrf.trace_data_2)
    {
        temp = setNumContinuousOutsideMaskCount(it.first,value);
        if( !temp )
        {
            r = false;
        }
    }
    return r;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setNumContinuousOutsideMaskCount(const std::string&name, size_t value)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(name).num_continuous_outside_mask_count = value;
        return true;
    }
    message("liberallrfInterface::setInfiniteMasks ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::checkForOutsideMaskTrace()
{
    for( auto& it: llrf.trace_data_2) // loop over each trace
    {
        if( llrf.check_mask && it.second.check_mask && it.second.hi_mask_set && it.second.lo_mask_set ) // if everything is ok, check mask
        {
            int result = updateIsTraceInMask(it.second);
            handleTraceInMaskResult(it.second,result);
        }
    }
}
//--------------------------------------------------------------------------------------------------
int liberallrfInterface::updateIsTraceInMask(llrfStructs::rf_trace_data_new& trace)
{
    /* to fail, you must get consecutive points outside the mask */
    size_t hi_breakdown_count = UTL::ZERO_INT;
    size_t lo_breakdown_count = UTL::ZERO_INT;
    /* ref for ease of reading */
    auto& to_check = trace.data_buffer.back().second;
    auto& hi = trace.hi_mask;
    auto& lo = trace.lo_mask;
    /* main loop iterate over the trace values  */
    for(auto i = 0; i < to_check.size(); ++i)
    {
        /* if we are above the mask floor */
        if(to_check[i] > trace.mask_floor)
        {
            /* if we are above the mask increase hi_breakdown_count */
            if(to_check[i] > hi[i])
            {
                hi_breakdown_count += UTL::ONE_SIZET;
                /* if we have too many consecutive hi_breakdown_count trace fails */
                if(hi_breakdown_count == trace.num_continuous_outside_mask_count)
                {
                    /* write a message*/
                    outside_mask_trace_message << trace.name << " HI MASK FAIL: current average = "
                    << trace.rolling_average[i] << ", " << to_check[i] << " > "
                    << hi[i] << " at i = " << i << " us = " << llrf.time_vector[i];
//                        << ", EVID " << trace.traces[trace.evid_current_trace].EVID << ", previous EVID = "
//                        <<trace.traces[trace.previous_evid_trace].EVID;
                    message(outside_mask_trace_message.str());
                    trace.outside_mask_index = i;
                    /* return code 0 = FAIL */
                    return UTL::ZERO_INT;
                }
            }
            else
            {
                /* if the value is good reset hi_breakdown_count to zero */
                hi_breakdown_count = UTL::ZERO_INT;
            }
            /* if we are belwo the lo mask, increase lo_breakdown_count */
            if(to_check[i] < lo[i])
            {
                lo_breakdown_count += UTL::ONE_INT;
                /* if we have too many consecutive lo_breakdown_count trace fails */
                if(lo_breakdown_count == trace.num_continuous_outside_mask_count)
                {
                    /* write a message*/
                    outside_mask_trace_message << trace.name << " LO MASK FAIL: current average = "
                    << trace.rolling_average[i] << ", " << to_check[i] << " < "
                    << lo[i] << " at i = " << i << " us = " << llrf.time_vector[i];
//                        << ", EVID " << trace.traces[trace.evid_current_trace].EVID << ", previous EVID = "
//                        <<trace.traces[trace.previous_evid_trace].EVID;
                    message(outside_mask_trace_message.str());
                    trace.outside_mask_index = i;
                    /* return code 0 = FAIL */
                    return UTL::ZERO_INT;
                }
            }
            else
            {
                /* if the value is good reset lo_breakdown_count to zero */
                lo_breakdown_count = UTL::ZERO_INT;
            }

        }//if(to_check[i] > trace.mask_floor)

    }//for(auto i = 0; i < to_check.size(); ++i)
    /* return code 1 = PASS */
    return UTL::ONE_INT;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::handleTraceInMaskResult(llrfStructs::rf_trace_data_new& trace, int result)
{
    //message(trace.name, "TraceInMaskResult = ", result );
    switch(result)
    {
        case UTL::ONE_INT: /* passed mask */
            handlePassedMask(trace);
            break;
        case UTL::ZERO_INT: /* failed mask */
            handleFailedMask(trace);
            break;
        case UTL::MINUS_ONE_INT:
            /* not checking masks or no active pulses */
            break;
    }
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::handlePassedMask(llrfStructs::rf_trace_data_new& trace)
{
    // err... do nothing
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getOutsideMaskEventCount()const
{
    return llrf.omed_count;
}
//---------------------------------------------------------------------------------------------------------
void liberallrfInterface::handleFailedMask(llrfStructs::rf_trace_data_new& trace)
{
    llrf.omed_count += UTL::ONE_SIZET;
    // Stop Checking Masks
    // first Drop the Amplitude
    if(trace.drop_amp_on_breakdown)
    {
        /* stop checking masks */
        message("setGlobalCheckMask(false);");
        setGlobalCheckMask(false);
        /* set amp to drop_value */
        message("setting amp to = ", trace.amp_drop_value, ", time = ",  elapsedTime());
        setAmpSPCallback(trace.amp_drop_value);
    }
    newOutsideMaskEvent(trace);
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::setAmpHP(double value)
{
    setAmpSPCallback(value);
    return true;
}
//---------------------------------------------------------------------------------------------------------
void liberallrfInterface::kill_finished_setAmpHP_threads()
{
    for(auto && it = setAmpHP_Threads.cbegin(); it != setAmpHP_Threads.cend() /* not hoisted */; /* no increment */)
    {
        if(it->can_kill)
        {
            /// join before deleting...
            /// http://stackoverflow.com/questions/25397874/deleting-stdthread-pointer-raises-exception-libcabi-dylib-terminating
            //std::cout<< "it->threadit->thread->join" <<std::endl;
            it->thread->join();
            //std::cout<< "it->thread" <<std::endl;
            delete it->thread;
            //std::cout<< "delete" <<std::endl;
            setAmpHP_Threads.erase(it++);
            //std::cout<< "erase" <<std::endl;
        }
        else
        {
            ++it;
        }
    }
}
//---------------------------------------------------------------------------------------------------------
void liberallrfInterface::setAmpSPCallback(const double value)
{
    setAmpHP_Threads.push_back(llrfStructs::setAmpHP_Struct());
    setAmpHP_Threads.back().interface = this;
    setAmpHP_Threads.back().value  = value;
    setAmpHP_Threads.back().thread = new std::thread(staticEntrySetAmp, std::ref(setAmpHP_Threads.back()));
    kill_finished_setAmpHP_threads();
}
//---------------------------------------------------------------------------------------------------------
void liberallrfInterface::staticEntrySetAmp(llrfStructs::setAmpHP_Struct& s)
{
    /*
        time how long this function takes to run
    */
    std::chrono::high_resolution_clock::time_point start1 = std::chrono::high_resolution_clock::now();

    s.interface->attachTo_thisCAContext(); /// base member function

    s.interface->OME_interlock(s);

    s.can_kill = true;
    /*
        time how long this function takes to run
    */
    std::chrono::high_resolution_clock::time_point end1 = std::chrono::high_resolution_clock::now();
    s.interface->message("thread took ", std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1).count(), " us");
}
//---------------------------------------------------------------------------------------------------------
void liberallrfInterface::OME_interlock(const llrfStructs::setAmpHP_Struct& s)
{
    setInterlockActive();

    resetToValue(s.value);

//    setAmpSP(s.value);
//    pause_1();
//    setInterlockNonActive();
//    pause_1();
//    enableRFOutput();
//    llrfStructs::pvStruct& pvs =  llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_FF_AMP_LOCK_STATE);
//    ca_put(pvs.CHTYPE, pvs.CHID, &UTL::ONE_US);
//    pvs =  llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_FF_PHASE_LOCK_STATE);
//    ca_put(pvs.CHTYPE, pvs.CHID, &UTL::ONE_US);
//    sendToEpics("ca_put","","");
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::setInterlockActive()
{
    if( isInterlockNotActive() )
        return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::INTERLOCK),llrfStructs::INTERLOCK_STATE::ACTIVE);
    else
        return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::setInterlockNonActive()
{
    if( isInterlockActive() )
        return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::INTERLOCK),llrfStructs::INTERLOCK_STATE::NON_ACTIVE);
    else
        return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isInterlockActive() const
{
    return llrf.interlock_state == llrfStructs::INTERLOCK_STATE::ACTIVE;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isInterlockNotActive() const
{
    return llrf.interlock_state == llrfStructs::INTERLOCK_STATE::NON_ACTIVE;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::lockAmpFF()
{
    if(isAmpFFNotLocked())
        return setValue<short>(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_FF_AMP_LOCK_STATE), UTL::ONE_US);
    return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::lockPhaseFF()
{
    if(isPhaseFFNotLocked())
        return setValue<short>(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_FF_PHASE_LOCK_STATE),UTL::ONE_US);
    return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::unlockAmpFF()
{
    if(isAmpFFLocked())
        return setValue<short>(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_FF_AMP_LOCK_STATE),UTL::ZERO_US);
    return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::unlockPhaseFF()
{
    if(isPhaseFFLocked())
        return setValue<short>(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_FF_PHASE_LOCK_STATE),UTL::ZERO_US);
    return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::enableRFOutput()
{
    if(isNotRFOutput())
        return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_RF_OUTPUT),UTL::ONE_US);
    return true;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isRFOutput() const
{
    return llrf.rf_output;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isNotRFOutput() const
{
    return !isRFOutput();
}
bool liberallrfInterface::isFFLocked()const
//---------------------------------------------------------------------------------------------------------
{
    return isAmpFFLocked() && isPhaseFFLocked();
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isFFNotLocked()const
{
    return isAmpFFNotLocked() && isPhaseFFNotLocked();
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isAmpFFLocked()const
{
    return llrf.ff_amp_lock_state;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isAmpFFNotLocked()const
{
    return !isFFLocked();
}
//____________________________________________________________________________________________
bool liberallrfInterface::isPhaseFFLocked()const
{
    return llrf.ff_amp_lock_state;
}
//---------------------------------------------------------------------------------------------------------
bool liberallrfInterface::isPhaseFFNotLocked()const
{
    return !isPhaseFFLocked();
}

//____________________________________________________________________________________________
bool liberallrfInterface::RFOutput()const
{
    return llrf.rf_output;
}


//____________________________________________________________________________________________
void liberallrfInterface::newOutsideMaskEvent(const llrfStructs::rf_trace_data_new& trace)
{
    llrf.omed.is_collecting = true;
    llrf.omed.can_get_data = false;
    llrf.omed.num_collected = UTL::THREE_SIZET;
    llrf.omed.num_still_to_collect = llrf.omed.extra_traces_on_outside_mask_event;
    llrf.omed.trace_that_caused_OME = trace.name;

    llrf.omed.outside_mask_trace_message = trace.outside_mask_trace_message;
    llrf.omed.outside_mask_index = trace.outside_mask_index;

    llrf.omed.trace_data.clear();
    llrf.omed.hi_mask = trace.hi_mask;
    llrf.omed.lo_mask = trace.lo_mask;
    llrf.omed.mask_floor = trace.mask_floor;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::checkCollectingFutureTraces()
{
    if( llrf.omed.is_collecting )
    {
        llrf.omed.num_still_to_collect -= UTL::ONE_SIZET;
        llrf.omed.num_collected += UTL::ONE_SIZET;

        // sanity check
        if(  llrf.omed.num_still_to_collect == UTL::ZERO_SIZET )
        {

            if( llrf.omed.num_collected == llrf.omed.extra_traces_on_outside_mask_event + UTL::THREE_SIZET)
            {
                copyTraceDataToOMED();
                llrf.omed.is_collecting = false;
                llrf.omed.can_get_data  = true;
            }
            else
            {
                message("ERROR NEVER SHOW THIS MESSAGE");
            }
        }
        else
        {
            //message("Collecting future traces = ", llrf.omed.num_still_to_collect);
        }
    }
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::copyTraceDataToOMED()
{
    // loop over each traces_to_save_on_outside_mask_event
    // and copy the last 3 + extra_traces_on_outside_mask_event (probably 5)

    size_t num_traces_to_copy = llrf.omed.extra_traces_on_outside_mask_event + UTL::THREE_SIZET;

    for(auto&&trace_name:llrf.omed.traces_to_save_on_outside_mask_event )
    {
        llrf.omed.trace_data.push_back( llrfStructs::outside_mask_event_trace_data() );
        llrf.omed.trace_data.back().name = trace_name;
        llrf.omed.trace_data.back().hi_mask = llrf.trace_data_2.at(trace_name).hi_mask;
        llrf.omed.trace_data.back().lo_mask = llrf.trace_data_2.at(trace_name).lo_mask;
        for(auto i = num_traces_to_copy ; i > UTL::ZERO_SIZET; --i )
        {
            std::pair<epicsTimeStamp, std::vector<double>>& data_ref =  *(llrf.trace_data_2.at(trace_name).data_buffer.end() - i);

            double temp_d;
            updateTime( data_ref.first, temp_d, temp_OMED_trace_item.first);
            temp_OMED_trace_item.second = data_ref.second;
            llrf.omed.trace_data.back().data_buffer.push_back( temp_OMED_trace_item );

//            message(trace_name);
//            size_t i2 = 0;
//            for(auto i: data_ref.second)
//            {
//                std::cout << i << " ";
//                if( i2 == 20 )
//                {
//                    break;
//                }
//                i2 += 1;
//            }
//            std::cout << std::endl;
//            std::cout << std::endl;


        }
    }
    message("copyTraceDataToOMED has finished");
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setTracesToSaveOnOutsideMaskEvent(const std::vector<std::string>& name)
{
    std::vector<std::string> checked_names;
    for(auto&& i: name)
    {
        const std::string n = fullCavityTraceName(i);
        if(entryExists(llrf.trace_data_2,n))
        {
            checked_names.push_back(n);
        }
    }
    llrf.omed.traces_to_save_on_outside_mask_event = checked_names;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setTracesToSaveOnOutsideMaskEvent(const std::string& name)
{
    std::vector<std::string> n;
    n.push_back(name);
    setTracesToSaveOnOutsideMaskEvent(n);
}
//--------------------------------------------------------------------------------------------------
std::vector<std::string> liberallrfInterface::getTracesToSaveOnOutsideMaskEvent()
{
    return llrf.omed.traces_to_save_on_outside_mask_event;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setNumExtraTracesOnOutsideMaskEvent(const size_t value)
{
    llrf.omed.extra_traces_on_outside_mask_event = value;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getNumExtraTracesOnOutsideMaskEvent() const
{
    return llrf.omed.extra_traces_on_outside_mask_event;
}



//--------------------------------------------------------------------------------------------------
/// this needs to change, as we know each part is complete (this is an hangover from old trace data beign seperate
                                                            // just need an iscollecting fuction
bool liberallrfInterface::isOutsideMaskEventDataCollecting()const
{
    return llrf.omed.is_collecting;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::canGetOutsideMaskEventData()const
{
    return llrf.omed.can_get_data;
}


//--------------------------------------------------------------------------------------------------


//____________________________________________________________________________________________
size_t liberallrfInterface::getOutsideMaskEventDataSize()
{
    return llrf.omed.num_collected;
}

//____________________________________________________________________________________________
bool liberallrfInterface::setDropAmpOnOutsideMaskEvent(const std::string& name,const  bool state,const double amp_val)
{   // whether to drop amp when detetcing an outside_mask_trace
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, fullCavityTraceName(n)))
    {
        llrf.trace_data_2.at(n).amp_drop_value = amp_val;
        llrf.trace_data_2.at(n).drop_amp_on_breakdown  = state;
        return true;
    }
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setDropAmpOnOutsideMaskEventValue(const std::string& name, double amp_val)
{   // amplitude to set after detecting outside mask
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, fullCavityTraceName(n)))
    {
        llrf.trace_data_2.at(n).amp_drop_value = amp_val;
        return true;
    }
    return false;
}
//____________________________________________________________________________________________
llrfStructs::outside_mask_event_data liberallrfInterface::getOutsideMaskEventData()
{
    return llrf.omed;
}

//____________________________________________________________________________________________
const llrfStructs::outside_mask_event_data& liberallrfInterface::getOutsideMaskEventData_CRef()
{
    return llrf.omed;
}














// OLD DO NOT USE
//____________________________________________________________________________________________
int liberallrfInterface::updateIsTraceInMask(llrfStructs::rf_trace_data& trace)
{

    // NEEDS A COMPLETE RE_WRITE
    // NEEDS A COMPLETE RE_WRITE
    // NEEDS A COMPLETE RE_WRITE
    // NEEDS A COMPLETE RE_WRITE
    // NEEDS A COMPLETE RE_WRITE

    // is trace in masks is only checked if we are increasing the active pulses!!!
    /* only check active pulses, */
    /// this may be causing a bug: the klystron could be giving power but the traces from
    /// before the klystron became active are still being processed
    if(llrf.can_increase_active_pulses)// only check active pulses
    {
        /* to fail, you must get consecutive points outside the mask */
        size_t hi_breakdown_count = UTL::ZERO_INT;
        size_t lo_breakdown_count = UTL::ZERO_INT;
        /* ref for ease of reading */
        auto & to_check = trace.traces[trace.current_trace].value;
        auto & hi = trace.high_mask;
        auto & lo = trace.low_mask;
        /* main loop iterate over the trace values  */
        for(auto i = 0; i < to_check.size(); ++i)
        {
            /* if we are above the mask floor */
            if(to_check[i] > trace.mask_floor)
            {
                /* if we are above the mask increase hi_breakdown_count */
                if(to_check[i] > hi[i])
                {
                    hi_breakdown_count += 1;
                    /* if we have too many consecutive hi_breakdown_count trace fails */
                    if(hi_breakdown_count == trace.num_continuous_outside_mask_count)
                    {
                        /* write a message*/
                        outside_mask_trace_message << trace.name << " HI MASK FAIL: current average = "
                        << trace.rolling_average[i] << ", " << to_check[i] << " > "
//                        << hi[i] << " at i = " << i << " us = " << llrf.time_vector.value[i]
                        << ", EVID " << trace.traces[trace.evid_current_trace].EVID << ", previous EVID = "
                        <<trace.traces[trace.previous_evid_trace].EVID;
                        message(outside_mask_trace_message.str());
                        trace.outside_mask_index = i;
                        /* return code 0 = FAIL */
                        return UTL::ZERO_INT;
                    }
                }
                else
                {
                    /* if the value is good reset hi_breakdown_count to zero */
                    hi_breakdown_count = UTL::ZERO_INT;
                }
                /* if we are above the mask increase lo_breakdown_count */
                if(to_check[i] < lo[i])
                {
                    lo_breakdown_count += UTL::ONE_INT;
                    /* if we have too many consecutive lo_breakdown_count trace fails */
                    if(lo_breakdown_count == trace.num_continuous_outside_mask_count)
                    {
                        /* write a message*/
                        outside_mask_trace_message << trace.name << " LO MASK FAIL: current average = "
                        << trace.rolling_average[i] << ", " << to_check[i] << " < "
//                        << lo[i] << " at i = " << i << " us = " << llrf.time_vector.value[i]
                        << ", EVID " << trace.traces[trace.evid_current_trace].EVID << ", previous EVID = "
                        <<trace.traces[trace.previous_evid_trace].EVID;
                        message(outside_mask_trace_message.str());
                        trace.outside_mask_index = i;
                        /* return code 0 = FAIL */
                        return UTL::ZERO_INT;
                    }
                }
                else
                {
                    /* if the value is good reset lo_breakdown_count to zero */
                    lo_breakdown_count = UTL::ZERO_INT;
                }

            }//if(to_check[i] > trace.mask_floor)

        }//for(auto i = 0; i < to_check.size(); ++i)
        /* return code 1 = PASS */
        return UTL::ONE_INT;

    }//if(llrf.can_increase_active_pulses)
    /* return code -1 = couldn't check mask, pulses not active */
    return UTL::MINUS_ONE_INT;
}
//____________________________________________________________________________________________
void liberallrfInterface::handleTraceInMaskResult(llrfStructs::rf_trace_data& trace, int result)
{
    switch(result)
    {
        case UTL::ONE_INT: /* passed mask */
            handlePassedMask(trace);
            break;
        case UTL::ZERO_INT: /* failed mask */
            handleFailedMask(trace);
            break;
        case UTL::MINUS_ONE_INT:
            /* not checking masks or no active pulses */
            break;
    }
}
// OLD DO NOT USE ???
//____________________________________________________________________________________________
bool liberallrfInterface::shouldCheckMasks(llrfStructs::rf_trace_data& trace)
{
    //debugMessage("shouldCheckMasks = ", trace.check_mask, trace.hi_mask_set, trace.low_mask_set);
    return llrf.check_mask && trace.check_mask && trace.hi_mask_set && trace.low_mask_set;
}
//____________________________________________________________________________________________
bool liberallrfInterface::shouldCheckMasks(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        bool a = shouldCheckMasks(llrf.trace_data.at(n));
        if(a)
        {
        }
        else
        {
            message(n,
                    " llrf.check_mask = ",llrf.check_mask,
                    ", trace.check_mask =  " ,llrf.trace_data.at(n).check_mask,
                    ", trace.hi_mask_set =  " ,llrf.trace_data.at(n).hi_mask_set,
                    ", trace.low_mask_set =  " ,llrf.trace_data.at(n).low_mask_set
                   );
        }
        return a;

    }
    return false;
}














//--------------------------------------------------------------------------------------------------
// NOT USED ANYMORE
bool liberallrfInterface::set_mask(const size_t s1,const size_t s2,const size_t s3,const size_t s4,
                      const double value, const std::string& name,const bool isPercent)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    // automatically set the mask based on the rolling_average for cavity_rev_power trace
    // between element 0    and s1 will be set to default hi/lo (+/-infinity)
    // between element s1+1 and s2 will be set by rolling_average +/- value percent of rolling_average
    // between element s2+1 and s3 will be set very default hi/lo (+/-infinity)
    // between element s3+1 and s4 will be set by rolling_average +/- value percent of rolling_average
    // between element s3+1 and s4 will be set very default hi/lo (+/-infinity)
    double temp = 0.0;
    std::string mask_type;
    if(isPercent)
    {
        mask_type = "setPercentMask";
    }
    else
    {
        mask_type = "setAbsoluteMask";
    }
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        // if we're keeping an average pulse
        if(llrf.trace_data.at(n).has_average)
        {
            std::vector<double> & ra = llrf.trace_data.at(n).rolling_average;

            // sanity check on s1,s2,s3,s4
            if(0 <= s1 && s4 <= ra.size() - 1)
            {
                if(s1 <= s2 && s2 <= s3 && s3 <= s4)
                {
                    double hi_max = -std::numeric_limits<double>::infinity();
                    double lo_min =  std::numeric_limits<double>::infinity();

                    std::vector<double> hi_mask(ra.size(), 0.0);
                    std::vector<double> lo_mask(ra.size(), 0.0);

                    for(auto i = 0; i <= s1; ++i)
                    {
                        hi_mask[i] =   std::numeric_limits<double>::infinity();
                        lo_mask[i] = - std::numeric_limits<double>::infinity();
                    }
                    for(auto i = s1; i <= s2; ++i)
                    {
                        if(isPercent)
                        {
                            temp = ra[i] * value;
                        }
                        else
                        {
                            temp = value;
                        }
                        hi_mask[i] = ra[i] + temp;
                        lo_mask[i] = ra[i] - temp;
                        if(hi_mask[i] > hi_max)
                        {
                           hi_max = hi_mask[i];
                        }
                        if(lo_mask[i] < lo_min)
                        {
                           lo_min = hi_mask[i];
                        }
                    }
                    for(auto i = s2+1; i <= s3; ++i)
                    {
                        hi_mask[i] =   std::numeric_limits<double>::infinity();
                        lo_mask[i] = - std::numeric_limits<double>::infinity();
                    }
                    for(auto i = s3+1; i <= s4; ++i)
                    {
                        if(isPercent)
                        {
                            temp = ra[i] * value;
                        }
                        else
                        {
                            temp = value;
                        }
                        hi_mask[i] = ra[i] + temp;
                        lo_mask[i] = ra[i] - temp;
                        if(hi_mask[i] > hi_max)
                        {
                           hi_max = hi_mask[i];
                        }
                        if(lo_mask[i] < lo_min)
                        {
                           lo_min = hi_mask[i];
                        }
                    }
                    for(auto i = s4+1; i < ra.size(); ++i)
                    {
                        hi_mask[i] =   std::numeric_limits<double>::infinity();
                        lo_mask[i] = - std::numeric_limits<double>::infinity();
                    }
                    // apply mask values
                    if(setHiMask(n,hi_mask))
                    {
                        message("hi_max = ",hi_max);
                        if(setLoMask(n,lo_mask))
                        {
                            message("lo_min = ", lo_min);
                            return true;
                        }
                        else
                        {
                            message(n," setLowMask failed,  ",mask_type," fail");
                        }
                    }
                    else
                    {
                        message(n," setHighMask failed,,  ",mask_type," fail");
                    }
                }
                else
                {
                    message(n,s1," <= ",s2," && ",s2," <= ",s3," && ",s3," <= ",s4," failed,  ",mask_type," fail");
                }
            }
            else
            {
                message(n," 0 <= ",s1," && ",s4," <= ",ra.size() - 1," failed, ",mask_type," fail");
            }
        }
        else
        {
            message(n," does not exist,  ",mask_type," fail");
        }
    }
    else
    {
        message(n," does not exist,  ",mask_type," fail");
    }
    return false;
}


































/*
     __   __                    __                ___  __        __   ___  __
    |__) /  \ |    |    | |\ | / _`     /\  \  / |__  |__)  /\  / _` |__  /__`
    |  \ \__/ |___ |___ | | \| \__>    /~~\  \/  |___ |  \ /~~\ \__> |___ .__/

// Rolling averages for each individual trace can be kept
*/
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateTraceRollingAverage(llrfStructs::rf_trace_data_new& data )
{
    //std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    // call heirarchy:
    // updateAllTraces(const event_handler_args& args)
    // updateMasks()
    //
    //
    if(data.keep_rolling_average)
    {
        if(data.rolling_average_size == UTL::ZERO_SIZET)
        {

        }
        else if(data.rolling_average_size == UTL::ONE_SIZET)
        {
            data.rolling_average = data.data_buffer.back().second;
            data.rolling_average_count = data.rolling_average_size;
            data.has_average = true;
        }
        else
        {
            // append new data to average_trace_values
            data.average_trace_values.push( data.data_buffer.back().second );

            // add the new trace to data.rolling_sum
            std::vector<double>& to_add = data.average_trace_values.back();
            std::vector<double>& sum    = data.rolling_sum;

            for(auto&& sum_it = sum.begin(), to_add_it = to_add.begin();
                       sum_it < sum.end() && to_add_it < to_add.end();
                       ++to_add_it         , ++sum_it )
            {
                *sum_it += *to_add_it;
            }

            // if we have aquuired data.average_size traces then we can take an average
            if(data.average_trace_values.size() == data.rolling_average_size)
            {
                debugMessage(data.name, "  has_average = true ", data.average_trace_values.size()," == ",data.rolling_average_size);
                data.has_average = true;
            }
            //
            if(data.average_trace_values.size() > data.rolling_average_size)
            {
//                debugMessage(data.name, "  average_trace_values.size() > data.rolling_average_size ",
//                             data.average_trace_values.size()," >  ",data.rolling_average_size);
                // if so subtract the first trace in average_trace_values
                std::vector<double>& to_sub = data.average_trace_values.front();
                for(auto i1=to_sub.begin(), i2=sum.begin(); i1<to_sub.end() && i2<sum.end(); ++i1,++i2)
                {
                    *i2 -= *i1;
                }
                // erase the trace we just subtraced
                data.average_trace_values.pop();
            }
            if(data.has_average)
            {
                //message("has_average");
                std::vector<double>& av  = data.rolling_average;
                double s = (double)data.rolling_average_size;
                for(auto i1=sum.begin(), i2=av.begin(); i1<sum.end() && i2<av.end(); ++i1,++i2)
                {
                    *i2 = *i1 / s;
                }
            }
            data.rolling_average_count = data.average_trace_values.size();
        }
    }
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::clearAllRollingAverage()
{
    for(auto && it : llrf.trace_data_2)
    {
        clearTraceRollingAverage(it.second);
    }
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::clearTraceRollingAverage(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        clearTraceRollingAverage(llrf.trace_data_2.at(n));
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::clearTraceRollingAverage(llrfStructs::rf_trace_data_new& trace)
{
    trace.has_average = false;
    trace.current_trace = UTL::ZERO_SIZET;
    trace.previous_trace = UTL::MINUS_ONE_INT;
    trace.previous_previous_trace = UTL::MINUS_TWO_INT;
    trace.rolling_average_count = UTL::ZERO_SIZET;
    trace.rolling_average.clear();
    trace.rolling_average.resize(trace.trace_size);
    trace.rolling_sum.clear();
    trace.rolling_sum.resize(trace.trace_size);
    trace.average_trace_values = {};
    //trace.average_trace_values.resize(trace.average_size);
    trace.rolling_max.clear();
    trace.rolling_max.resize(trace.trace_size, -std::numeric_limits<double>::infinity());
    trace.rolling_min.clear();
    trace.rolling_min.resize(trace.trace_size, std::numeric_limits<double>::infinity());
    trace.rolling_sd.clear();
    trace.rolling_sd.resize(trace.trace_size);

    message("clearTraceRollingAverage FIN ");


}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setShouldKeepRollingAverage()
{
    for(auto && it: llrf.trace_data_2)
    {
        setShouldKeepRollingAverage(it.first);
    }
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setShouldNotKeepRollingAverage()
{
    for(auto && it: llrf.trace_data_2)
    {
        setShouldNotKeepRollingAverage(it.first);
    }
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setShouldKeepRollingAverage(const std::string&name)
{
    return setKeepRollingAverage(name, true);
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setShouldNotKeepRollingAverage(const std::string&name)
{
    return setKeepRollingAverage(name, false);
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setKeepRollingAverage(const std::string&name, bool value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).keep_rolling_average = value;
        clearTraceRollingAverage(llrf.trace_data_2.at(n));
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setKeepRollingAverageNoReset(const bool value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    for(auto && it: llrf.trace_data_2)
    {
        it.second.keep_rolling_average = value;
    }
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setKeepRollingAverageNoReset(const std::string&name, const bool value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).keep_rolling_average = value;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setAllRollingAverageSize(const size_t value)
{
    for(auto && it: llrf.trace_data_2)
    {
        setTraceRollingAverageSize(it.first, value);
    }
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setTraceRollingAverageSize(const std::string&name,const size_t value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        llrf.trace_data_2.at(n).rolling_average_size = value;
        clearTraceRollingAverage(llrf.trace_data_2.at(n));
        return true;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
std::vector<double> liberallrfInterface::getTraceRollingAverage(const std::string&name)const
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).rolling_average;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    std::vector<double> d(llrf.all_traces.num_elements_per_trace_used,UTL::DUMMY_DOUBLE);
    return d;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getTraceRollingAverageSize(const std::string&name)const
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).rolling_average_size;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return UTL::ZERO_SIZET;
}
//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getTraceRollingAverageCount(const std::string&name)const
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).rolling_average_count;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return UTL::ZERO_SIZET;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::isKeepingRollingAverage(const std::string&name)const
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).keep_rolling_average;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::hasRollingAverage(const std::string&name)const
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2, n))
    {
        return llrf.trace_data_2.at(n).has_average;
    }
    message("liberallrfInterface ERROR, trace ", n, " does not exist");
    return false;
}











































//--------------------------------------------------------------------------------------------------
/*
    ___  __        __   ___     __        ___  ___  ___  __      __    __  ___
     |  |__)  /\  /  ` |__     |__) |  | |__  |__  |__  |__)    /__` |  / |__
     |  |  \ /~~\ \__, |___    |__) \__/ |    |    |___ |  \    .__/ | /_ |___


    Set the number of Buffer Traces in the ON_RECORD trace data
    Set the number of buffer traces held in the individual trace buffers
*/
bool liberallrfInterface::setAllTraceBufferSize(const size_t value)
{
    // min buffer size is FIVE
    size_t new_val = UTL::FIVE_SIZET;
    if(value > UTL::FIVE_SIZET)
    {
        new_val  = value;
    }

    std::vector<double> temp2( (size_t)llrf.all_traces.num_traces * llrf.all_traces.num_elements_per_trace, UTL::ZERO_DOUBLE);

    // temp_all_trace_item is used to push_back each call to updateTraces

    temp_all_trace_item.first = epicsTimeStamp();
    temp_all_trace_item.second = temp2;

    llrf.all_traces.data_buffer.assign( new_val, temp_all_trace_item);
    message("All-trace buffer size = ", llrf.all_traces.data_buffer.size());
    return llrf.all_traces.data_buffer.size() == new_val;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setIndividualTraceBufferSize(const size_t value)
{
    bool ret = true;
    bool temp;
    for(auto && it: llrf.trace_data_2)
    {
        temp = setIndividualTraceBufferSize(it.first, value);
        if(!temp)
        {
            ret = false;
        }
    }
    return ret;
}
//--------------------------------------------------------------------------------------------------
bool liberallrfInterface::setIndividualTraceBufferSize(const std::string&name, const size_t value)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    std::string full_name = fullCavityTraceName(name);
    std::vector<double> t( llrf.all_traces.num_elements_per_trace_used, UTL::ZERO_DOUBLE );

    // temp_trace_item is used to push_back each individual trace in updateAllTarces
    temp_trace_item.first = epicsTimeStamp();
    temp_trace_item.second = t;

    if(entryExists(llrf.trace_data_2, full_name))
    {
        llrf.trace_data_2.at(full_name).buffersize = value;
        llrf.trace_data_2.at(full_name).data_buffer.assign(value , temp_trace_item);

        llrf.trace_data_2.at(full_name).trace_size = llrf.all_traces.num_elements_per_trace_used;

        message(full_name," individual-trace buffer size = ", llrf.trace_data_2.at(full_name).data_buffer.size());
        return llrf.trace_data_2.at(full_name).data_buffer.size() == value;
    }
    return false;
}



//--------------------------------------------------------------------------------------------------
/*        __  ___         ___     __             __   ___  __
     /\  /  `  |  | \  / |__     |__) |  | |    /__` |__  /__`
    /~~\ \__,  |  |  \/  |___    |    \__/ |___ .__/ |___ .__/
*/
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateActivePulses()
{
    /*  if we get exactly the same max as previous pulse we assume
        its a repeat and don't increase pulse count */
    if(llrf.last_kly_fwd_power_max == llrf.kly_fwd_power_max)
    {
        llrf.can_increase_active_pulses = false;
    }
    else if(llrf.kly_fwd_power_max > llrf.active_pulse_kly_power_limit)
    {
        llrf.can_increase_active_pulses = true;
    }
    else
    {
        llrf.can_increase_active_pulses = false;
    }
    /* the llrf object holds this paramter as well as the trace */
    if(llrf.can_increase_active_pulses)
    {
        llrf.active_pulse_count += UTL::ONE_SIZET;
    }
    //message("AP max ", llrf.kly_fwd_power_max , " (", llrf.last_kly_fwd_power_max,") count = ",llrf.active_pulse_count, ", increase  = ",llrf.can_increase_active_pulses);
    llrf.last_kly_fwd_power_max  = llrf.kly_fwd_power_max;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getActivePulsePowerLimit() const
{
    return llrf.active_pulse_kly_power_limit;
}
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::setActivePulsePowerLimit(const double value)
{
    llrf.active_pulse_kly_power_limit = value;
}
//____________________________________________________________________________________________
size_t liberallrfInterface::getActivePulseCount()const
{
    return llrf.active_pulse_count;
}
//____________________________________________________________________________________________
void liberallrfInterface::setActivePulseCount(const size_t value)
{
    llrf.active_pulse_count = value;
}
//____________________________________________________________________________________________
void liberallrfInterface::addActivePulseCountOffset(const size_t val)
{
    llrf.active_pulse_count += val;
}







//--------------------------------------------------------------------------------------------------
/*
    ___  __        __   ___                          ___  __
     |  |__)  /\  /  ` |__      \  /  /\  |    |  | |__  /__`
     |  |  \ /~~\ \__, |___      \/  /~~\ |___ \__/ |___ .__/

    Simple processing for traces to find the max, mean etc.
*/
double liberallrfInterface::getTraceMax(const std::string& name)
{
    if(entryExists(llrf.trace_data_2, name))
    {
        llrf.trace_data_2.at(name).trace_max =
            *max_element(std::begin( llrf.trace_data_2.at(name).data_buffer.back().second ),
                         std::end  ( llrf.trace_data_2.at(name).data_buffer.back().second ) );
        return llrf.trace_data_2.at(name).trace_max;
    }
    return UTL::DUMMY_DOUBLE;
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getKlyFwdPwrMax()
{
    return getTraceMax(UTL::KLYSTRON_FORWARD_POWER);
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getKlyFwdPhaMax()
{
    return getTraceMax(UTL::KLYSTRON_FORWARD_PHASE);
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getKlyRevPwrMax()
{
    return getTraceMax(UTL::KLYSTRON_REVERSE_POWER);
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getKlyRevPhaMax()
{
    return getTraceMax(UTL::KLYSTRON_REVERSE_PHASE);
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getCavFwdPwrMax()
{
    return getTraceMax(fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getCavFwdPhaMax()
{
    return getTraceMax(fullCavityTraceName(UTL::CAVITY_FORWARD_PHASE));
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getCavRevPwrMax()
{
    return getTraceMax(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getCavRevPhaMax()
{
    return getTraceMax(fullCavityTraceName(UTL::CAVITY_REVERSE_PHASE));
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getProbePwrMax()
{
    return getTraceMax(UTL::CAVITY_PROBE_POWER);
}
//--------------------------------------------------------------------------------------------------
double liberallrfInterface::getProbePhaMax()
{
    return getTraceMax(UTL::CAVITY_PROBE_PHASE);
}
//--------------------------------------------------------------------------------------------------
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getTraceValues(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data_2,n))
    {
        return llrf.trace_data_2.at(n).data_buffer.back().second;
    }
    message("liberallrfInterface::getTraceValues ERROR, trace ", n, " does not exist");
    std::vector<double> r(UTL::ONE_SIZET, UTL::DUMMY_DOUBLE);//MAGIC_NUMBER
    return r;
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavRevPower()
{
    return getTraceValues(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavRevPhase()
{
    return getTraceValues(fullCavityTraceName(UTL::CAVITY_REVERSE_PHASE));
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavFwdPower()
{
    return getTraceValues(fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavFwdPhase()
{
    return getTraceValues(fullCavityTraceName(UTL::CAVITY_FORWARD_PHASE));
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyRevPower()
{
    return getTraceValues(UTL::KLYSTRON_REVERSE_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyRevPhase()
{
    return getTraceValues(UTL::KLYSTRON_REVERSE_PHASE);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyFwdPower()
{
    return getTraceValues(UTL::KLYSTRON_FORWARD_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyFwdPhase()
{
    return getTraceValues(UTL::KLYSTRON_FORWARD_PHASE);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getProbePower()
{
    return getTraceValues(UTL::CAVITY_PROBE_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getProbePhase()
{
    return getTraceValues(UTL::CAVITY_PROBE_PHASE);
}

//--------------------------------------------------------------------------------------------------
size_t liberallrfInterface::getIndex(const double time)
{
    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
    auto r = std::lower_bound(llrf.time_vector.begin(), llrf.time_vector.end(),time);
    return r - llrf.time_vector.begin();
}
//____________________________________________________________________________________________
bool liberallrfInterface::setMeanStartEndTime(const double start, const double end, const std::string&name)
{
    bool a = setMeanStartIndex(name, getIndex(start));
    if(a)
    {
        a = setMeanStopIndex(name, getIndex(end));
    }
    return a;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setMeanStartIndex(const std::string&name, size_t  value)
{
    if(entryExists(llrf.trace_data, name))
    {
        if(llrf.trace_data.at(name).trace_size - 1 >= value )
        {
            llrf.trace_data.at(name).mean_start_index = value;
            return true;
        }
    }
//    else
//        message(name," NOT FOUND!");
    return false;
}
//____________________________________________________________________________________________
bool  liberallrfInterface::setMeanStopIndex(const std::string&name, size_t  value)
{
    if(entryExists(llrf.trace_data, name))
    {
        if(llrf.trace_data.at(name).trace_size - 1 >= value )
        {
            llrf.trace_data.at(name).mean_stop_index = value;
            return true;
        }
    }
    //message(name," NOT FOUND!");
    return false;
}











//____________________________________________________________________________________________
void liberallrfInterface::startTraceMonitoring()
{
    for(auto&& it:llrf.pvOneTraceMonStructs)
    {
        addTo_continuousMonitorStructs(it);
    }
}
//____________________________________________________________________________________________
bool liberallrfInterface::startTraceMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    if(Is_TracePV(pv))
    {
        if(isNotMonitoring(pv))
        {
            std::string name = getLLRFChannelName(pv);
            //llrf.trace_data[name].traces.resize(llrf.trace_data[name].buffersize);
            //for(auto && it2: llrf.trace_data[name].traces)
            //    it2.value.resize(llrf.pvMonStructs.at(pv).COUNT);

            debugMessage("ca_create_subscription to ", ENUM_TO_STRING(pv), " (",getLLRFChannelName(pv) ,")");
            continuousMonitorStructs.push_back(new llrfStructs::monitorStruct());
            continuousMonitorStructs.back() -> monType   = pv;
            continuousMonitorStructs.back() -> llrfObj   = &llrf;
            continuousMonitorStructs.back() -> interface = this;
            continuousMonitorStructs.back() -> name      = getLLRFChannelName(pv);
            ca_create_subscription(llrf.pvMonStructs.at(pv).CHTYPE,
                               llrf.pvMonStructs.at(pv).COUNT,
                               llrf.pvMonStructs.at(pv).CHID,
                               llrf.pvMonStructs.at(pv).MASK,
                               liberallrfInterface::staticEntryLLRFMonitor,
                               (void*)continuousMonitorStructs.back(),
                               &continuousMonitorStructs.back() -> EVID);
            std::stringstream ss;
            ss <<"liberallrfInterface succesfully subscribed to LLRF trace monitor: " << ENUM_TO_STRING(pv);
            std::string s1 = ss.str();
            ss.str(std::string());
            ss <<"!!TIMEOUT!! Subscription to LLRF Trace monitor " << ENUM_TO_STRING(pv) << " failed";
            std::string s2 = ss.str();

            /* makes sure the evid associated with the trace is also started */
            llrfStructs::LLRF_PV_TYPE EVIDpv = getEVID_pv(pv);
            if( EVIDpv != llrfStructs::LLRF_PV_TYPE::UNKNOWN )
            {
                debugMessage("ca_create_subscription to ", ENUM_TO_STRING(EVIDpv));
                continuousMonitorStructs.push_back(new llrfStructs::monitorStruct());
                continuousMonitorStructs.back() -> monType   = EVIDpv;
                continuousMonitorStructs.back() -> llrfObj   = &llrf;
                continuousMonitorStructs.back() -> interface = this;
                continuousMonitorStructs.back() -> name      = getLLRFChannelName(EVIDpv);
                ca_create_subscription(llrf.pvMonStructs.at(EVIDpv).CHTYPE,
                                       llrf.pvMonStructs.at(EVIDpv).COUNT,
                                       llrf.pvMonStructs.at(EVIDpv).CHID,
                                       llrf.pvMonStructs.at(EVIDpv).MASK,
                                       liberallrfInterface::staticEntryLLRFMonitor,
                                       (void*)continuousMonitorStructs.back(),
                                       &continuousMonitorStructs.back() -> EVID);
            }
            int status = sendToEpics("ca_create_subscription",s1.c_str(),s2.c_str());
            if (status == ECA_NORMAL)
            {
                debugMessage(ENUM_TO_STRING(pv)," Monitor Started");
            }
            else
            {
                killMonitor(continuousMonitorStructs.back());
            }
            return isMonitoring(pv);
        }
    }
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::startTraceMonitoring(const std::string& name)
{
    std::string n = fullCavityTraceName(name);
    debugMessage("STARTING ", n, ", getLLRFPVType(name) = ", getLLRFPVType(n));
    return startTraceMonitoring(getLLRFPVType(n));
}































// we're not going to do this as EVID is flakey
//--------------------------------------------------------------------------------------------------
void liberallrfInterface::updateAllTracesEVID(const event_handler_args& args)
{
//    bool update_buffer = false;
//    std::lock_guard<std::mutex> lg(mtx);  // This now locked your mutex mtx.lock();
//    const dbr_time_string* p = nullptr;
//    p = (const struct dbr_time_string*)args.dbr;
//    //message("m1a");
//
//    if(p != nullptr)
//    {
//        //message("m1aa");
//        if((*p).value == '\0')//MAGIC_STRING
//        {
//            llrf.all_traces.EVID_Str = "";
//            message("!!updateAllTracesEVID!!: null string passsed back by EPICS!!!!");
//        }
//        else
//        {
//            if( llrf.all_traces.EVID_Str == p->value )
//            {
//                //message("updateAllTracesEVID called with same EVID_Str as previous call, ", llrf.all_traces.EVID_Str);
//            }
//            else
//            {
//                update_buffer = true;
//                llrf.all_traces.EVID_Str = p->value;
//                //message("t.EVID ", t.EVID);
//
//                llrf.all_traces.EVID_etime = p->stamp;
//                updateTime(llrf.all_traces.EVID_etime,
//                           llrf.all_traces.EVID_time,
//                           llrf.all_traces.EVID_timeStr);
//
//                if(isDigits(llrf.all_traces.EVID_Str))//MAGIC_STRING
//                {
//                    llrf.all_traces.EVID = getNum(llrf.all_traces.EVID_Str);
//                }
//                //message("llrf.all_traces.EVID = ", llrf.all_traces.EVID);
//            }
//        }
//    }
//    else
//    {
//        llrf.all_traces.EVID_Str = "nullptr";
//        llrf.all_traces.EVID = UTL::MINUS_ONE_INT;
//    }
//
//    if(update_buffer)
//    {
//        /*
//            update buffer with latest values,
//            Q:? is it better to cretae a temp object and add that to buffer or,
//                should we push back the buffer and add straight to it??
//        */
//        //std::pair<int,std::string> temp;
//
//        llrf.all_traces.EVID_buffer.push_back();
//
//        llrf.all_traces.EVID_buffer.back().first = llrf.all_traces.EVID;
//        llrf.all_traces.EVID_buffer.back().second = llrf.all_traces.EVID_Str;
//
//
//        if(EVID_0 == -1)
//        {
//            EVID_0 = llrf.all_traces.EVID_buffer.back().first;
//
//            message("EVID_0 = ", EVID_0  );
//        }
//
//    //    message("New back: ", llrf.all_traces.EVID_buffer.back().first, " / ",llrf.all_traces.EVID_buffer.back().second);
//    //    message("New end1: ", (llrf.all_traces.EVID_buffer.end() - 1) ->first, " / ", (llrf.all_traces.EVID_buffer.end()-1)->second);
//    //    message("New end2: ", (llrf.all_traces.EVID_buffer.end() - 2) ->first, " / ", (llrf.all_traces.EVID_buffer.end()-2)->second);
//
//        auto it1 = llrf.all_traces.EVID_buffer.end() - 1;
//        auto it2 = llrf.all_traces.EVID_buffer.end() - 2;
//
//        if( it1->first - it2->first  != UTL::ONE_INT )
//        {
//            evid_miss_count += UTL::ONE_INT;
//            //message("WE LOST AN ALL-TRACE EVID, ", evid_miss_count );
//        }
//
//        evid_call_count += UTL::ONE_INT;
//
//        //message(evid_call_count, ", ",llrf.all_traces.EVID, " - ", EVID_0," = ", llrf.all_traces.EVID - EVID_0 );
//
//
//        //message("evid = , ", evid_call_count," / ", llrf.all_traces.EVID - EVID_0);
//
//
//        if( evid_call_count % 600 == 0 )
//        {
//            message(llrf.all_traces.EVID_timeStr, ", evid_call_count = , ", evid_call_count,
//                    " Lost EVID ", llrf.all_traces.EVID - EVID_0 - evid_call_count);
//        }
//
//    }

}








/*   _____  _____          _   _
    / ____|/ ____|   /\   | \ | |
   | (___ | |       /  \  |  \| |
    \___ \| |      / /\ \ | . ` |
    ____) | |____ / ____ \| |\  |
   |_____/ \_____/_/    \_\_| \_|
*/
//____________________________________________________________________________________________
bool liberallrfInterface::setTraceSCAN(const std::string& name, const llrfStructs::LLRF_SCAN value)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        for(auto&& it: llrf.pvMonStructs)
        {
            if(it.second.name == n && Is_SCAN_PV(it.first))
            {
                if(isMonitoring(it.first))
                {
                    return setValue(it.second,value);
                }
            }
        }
    }
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAllSCANToPassive()
{
    for(auto &&it:llrf.trace_scans)
    {
        //message("setAllSCANToPassive,  pvSCANStructs.size() = ", it.second.pvSCANStructs.size());
        for(auto&& it2: it.second.pvSCANStructs)
        {
            //message("set passive for ", ENUM_TO_STRING(it2.first));
            setValue(it2.second, llrfStructs::LLRF_SCAN::PASSIVE);
        }
    }
    return true;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setTORSCANToIOIntr()
{
    if( entryExists(llrf.pvOneTraceComStructs, llrfStructs::LLRF_PV_TYPE::ALL_TRACES_SCAN) )
    {
        return setValue(llrf.pvOneTraceComStructs.at(llrfStructs::LLRF_PV_TYPE::ALL_TRACES_SCAN),
                        llrfStructs::LLRF_SCAN::IO_INTR);
    }
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setTORACQMEvent()
{
    if( entryExists(llrf.pvOneTraceComStructs, llrfStructs::LLRF_PV_TYPE::ALL_TRACES_ACQM) )
    {
        return setValue(llrf.pvOneTraceComStructs.at(llrfStructs::LLRF_PV_TYPE::ALL_TRACES_ACQM),
                        llrfStructs::LLRF_ACQM::ACQM_EVENT);
    }
    return false;
}







//____________________________________________________________________________________________
//____________________________________________________________________________________________
bool liberallrfInterface::setAllTraceSCAN(const llrfStructs::LLRF_SCAN value)
{
    for(auto&& it: llrf.pvMonStructs)
    {
        if(Is_SCAN_PV(it.first))
            setValue(it.second,value);
    }
    return true;
}
//____________________________________________________________________________________________
bool liberallrfInterface::IsNot_SCAN_PV(llrfStructs::LLRF_PV_TYPE pv)
{
    return !Is_SCAN_PV(pv);
}
//____________________________________________________________________________________________
bool liberallrfInterface::Is_SCAN_PV(llrfStructs::LLRF_PV_TYPE pv)
{
    bool r = false;
//    if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_LOCAL_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_AMP_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_DER_SCAN)
//    {
//        r = true;
//    }
    return r;
}

























































































































//____________________________________________________________________________________________
void liberallrfInterface::updateTrace(const event_handler_args& args, llrfStructs::rf_trace_data& trace)
{   /* called from staticEntryLLRFMonitor, and will update a power or a phase trace */
    /* ref to trace to update */
//    auto& current_trace = trace.traces[trace.current_trace];
//
//    /* update the new trace values */
//    updateValues(args, current_trace);
//
//    /* find the maximum value in the trace */
//    trace.latest_max = *std::max_element(current_trace.value.begin(), current_trace.value.end());
//
//    /* if Klystron Forward Power, update can_increase_active_pulses, based on max value */
//    if(trace.name == UTL::KLYSTRON_FORWARD_POWER)
//    {
//        updateCanIncreaseActivePulses(trace.latest_max);
//    }
//    /* on detecting an outside-mask-trace we might be adding 'future traces'
//       the place to add them, and how many to add are contained in a vector of pairs:
//        outside_mask_data_index_to_add_future_traces_to
//       the first  number is the index  of outside_mask_traces to add data to
//       the second number is the number of traces STILL to add
//       when the second number gets to zero we shoudl delete the entry, as all data has been collected */
//    for(auto && it = trace.outside_mask_data_index_to_add_future_traces_to.begin();
//                it != trace.outside_mask_data_index_to_add_future_traces_to.end() /* not hoisted */; /* no increment */)
//    {
//        llrf.outside_mask_traces[it->first].traces.push_back(current_trace);
//        if(llrf.outside_mask_traces[it->first].traces.size() == llrf.outside_mask_traces[it->first].num_traces_to_collect)
//        {
//            llrf.outside_mask_traces[it->first].is_collecting = false;
//        }
//        it->second -= UTL::ONE_SIZET;
//        if(it->second == UTL::ZERO_SIZET)
//        {
//            trace.outside_mask_data_index_to_add_future_traces_to.erase(it);
//        }
//        else
//        {
//            ++it;
//        }
//    }
//
//    /* check masks */
//    if(shouldCheckMasks(trace))
//    {   //debugMessage("CHECKING MASKS ",trace.name);
//        int trace_in_mask_result = updateIsTraceInMask(trace);
//        handleTraceInMaskResult(trace, trace_in_mask_result);
//    }
//
//    /* calc means */
//    updateTraceCutMean(trace, current_trace);
//
//    /* the trace index tells us which part of 'traces' is the next to update
//       it circles 'round
//       first,set the ltest trace index, used for accesing latest data */
//    trace.latest_trace_index = trace.current_trace;
//    /* then update all indices */
//    updateTraceIndex(trace.current_trace, trace.traces.size());
//    updateTraceIndex(trace.previous_trace, trace.traces.size());
//    updateTraceIndex(trace.previous_previous_trace, trace.traces.size());
//    /* update shot count
//       the difference between this and pulse count indicates how many traces we miss! */
//    trace.shot += UTL::ONE_SIZET;
    //message("trace.shot = ", trace.shot);
}
//____________________________________________________________________________________________
void liberallrfInterface::updateValues(const event_handler_args& args, llrfStructs::rf_trace& trace)
{
    /* this function actually gets the new values from EPICS and adds them to the trace.value vector
       all LLRF traces should be updated using this function

    /* pointer to array we are expecting depends on type channel */
    const dbr_double_t * pValue;
    /* if time _type get time and set where pValue points to */
    if(isTimeType(args.type))
    {
        const dbr_time_double* p = (const struct dbr_time_double*)args.dbr;
        pValue = &p->value;
        trace.etime = p->stamp;
        updateTime(trace.etime, trace.time, trace.timeStr);
    }
    else /* set where pValue points to */
    {
        pValue = (dbr_double_t*)args.dbr;
    }
    /* copy array to trace, assumes array is of correct size! */
    std::copy(pValue, pValue+trace.value.size(), trace.value.begin());

    if(stringIsSubString(trace.name,UTL::PHASE))
    {
        //https://stackoverflow.com/questions/21452217/increment-all-c-stdvector-values-by-a-constant-value
        //message(trace.name," is a phase trace");
        std::transform(std::begin(trace.value),std::end(trace.value),std::begin(trace.value),[](double x){return x+UTL::ONEEIGHTY_ZERO_DOUBLE;});//MAGIC_NUMBER
    }
    else
    {
        //message(trace.name," is not a phase trace");
    }

}
////____________________________________________________________________________________________
//bool liberallrfInterface::isPhaseTrace(const std::string& name)
//{
//    if(name.find("PHASE") != std::string::npos)//MAGIC_STRING
//        return true;
//    else
//        return false;
//}

//____________________________________________________________________________________________
void liberallrfInterface::handlePassedMask(llrfStructs::rf_trace_data& trace)
{
    if(trace.keep_rolling_average)
    {
//        updateRollingAverage(trace);
    }
    if(trace.endInfiniteMask_Trace_Set)
    {
        // this should NOT move the end past the set limit ???
        // or have this as another option??
        auto& trace_bound   = trace.endMaskTrace_Bound;
        auto& power_trace_to_search = llrf.trace_data.at(trace_bound.first);
        auto& value_to_search  = power_trace_to_search.traces[power_trace_to_search.current_trace].value;

        //message(trace.name," looking for ", trace_bound.second, " in ", power_trace_to_search.name, " max = ",*std::max_element(value_to_search.begin(), value_to_search.end()));

        auto i = value_to_search.size()-1;
        for(; i>0 ;--i)
        {
            if(value_to_search[i] > trace_bound.second)
            {
                //auto idx = std::distance(begin(current_trace), bound.base()) - 1;
                //message("idx (us) == ", getTime(i));
                setMaskInfiniteEnd(trace.name, i);
                break;
            }
        }
//        auto bound = std::lower_bound(current_trace.rbegin(), current_trace.rend(), trace_bound.second);
//        if(bound != current_trace.rend())
//        {
//            auto idx = std::distance(begin(current_trace), bound.base()) - 1;
//            message("idx (us) == ", getTime(idx));
//            setMaskInfiniteEnd(trace.name, idx);
//        }
//        else
//        {
//            message("couldn't find ", trace_bound.second);
//        }
    }
}
//____________________________________________________________________________________________
void liberallrfInterface::handleFailedMask(llrfStructs::rf_trace_data& trace)
{
    addToOutsideMaskTraces(trace, trace.name);
}
//____________________________________________________________________________________________
void liberallrfInterface::addToOutsideMaskTraces(llrfStructs::rf_trace_data& trace,const std::string& name)
{
    if(trace.drop_amp_on_breakdown)
    {
        /* stop checking masks */
        //message("setGlobalCheckMask(false);");
        setGlobalCheckMask(false);
        /* set amp to drop_value */
        //message("setting amp to next_amp_drop = ",next_amp_drop, ", time = ",  elapsedTime());
        setAmpSPCallback(trace.amp_drop_value);
    }
    /* add new outside_mask_trace struct to outside_mask_traces */
    llrf.outside_mask_traces.push_back(llrfStructs::outside_mask_trace());
    /* local timem of outside_mask_trace */
    llrf.outside_mask_traces.back().time = elapsedTime();
    /* fill in data from where the trace that flagged an outside_mask trace */
    llrf.outside_mask_traces.back().trace_name = name;
    llrf.outside_mask_traces.back().high_mask  = trace.high_mask;
    llrf.outside_mask_traces.back().low_mask   = trace.low_mask;
    llrf.outside_mask_traces.back().mask_floor = trace.mask_floor;
    llrf.outside_mask_traces.back().time_vector = llrf.time_vector;
    /* index for element that was outside mask */
    llrf.outside_mask_traces.back().outside_mask_index = trace.outside_mask_index;
    /* turn on is_collecting, to get future traces */
    llrf.outside_mask_traces.back().is_collecting = true;
    /* add in message generated in updateIsTraceInMask */
    llrf.outside_mask_traces.back().message = outside_mask_trace_message.str();
    /* clear local copy of message */
    outside_mask_trace_message.str("");
    /* initialise number of traces to collect to zero */
    llrf.outside_mask_traces.back().num_traces_to_collect = UTL::ZERO_SIZET;
    /* set the part of outside_mask_traces to add future traces to
       and how many future traces to add  */
    std::pair<size_t, size_t> ouside_index_numtraces = std::make_pair(llrf.outside_mask_traces.size() - 1, llrf.num_extra_traces);
    /* save all the required traces (current, plus previous 2)
       set the save next trace flag, and the part of the outside_mask_traces to add to */
    for(auto && it: llrf.tracesToSaveOnBreakDown)
    {
        if(isMonitoring(it))
        {
            /* add to number of traces to collect */
            llrf.outside_mask_traces.back().num_traces_to_collect += (3 + llrf.num_extra_traces);// MAGIC_NUMBER
            /* ref for reading ease */
            llrfStructs::rf_trace_data& t = llrf.trace_data.at(it);
            /* add the oldest trace 1st */
            if(t.previous_previous_trace > UTL::MINUS_ONE_INT)
            {
                llrf.outside_mask_traces.back().traces.push_back(t.traces[ t.previous_previous_trace]);
                //message(trace.previous_previous_trace," added previous_previous_trace",trace.traces[trace.previous_previous_trace].EVID);
            }
            /* add the 2nd oldest trace */
            if(t.previous_trace > UTL::MINUS_ONE_INT)
            {
                llrf.outside_mask_traces.back().traces.push_back(t.traces[ t.previous_trace]);
                //message(trace.previous_trace," added previous_trace, ",trace.traces[trace.previous_trace].EVID);
            }
            /* add the latest  trace */
            llrf.outside_mask_traces.back().traces.push_back( t.traces[ t.current_trace]);

            t.outside_mask_data_index_to_add_future_traces_to.push_back(ouside_index_numtraces);

            //t.add_next_trace = llrf.num_extra_traces;
            //t.outside_mask_data_index_to_add_future_traces_to = true;
            // ... this part of outside_maask_trace, wehen the next update trace is called !
            //t.outside_mask_trace_part = llrf.outside_mask_traces.size();
        }
        else
            message(it, " IS NOT MONITORING So not Saving Data  (addToOutsideMaskTraces)");
    }
    llrf.num_outside_mask_traces = llrf.outside_mask_traces.size();

    // NOT WORKING OR TESTED
    llrf.breakdown_rate = (double)llrf.num_outside_mask_traces / (double)llrf.outside_mask_traces.back().time / 1000.;//MAGIC_NUMBER
    //debugMessage("Added trace to outside_mask_traces");
}


//____________________________________________________________________________________________
void liberallrfInterface::updateTraceCutMean(llrfStructs::rf_trace_data& tracedata,llrfStructs::rf_trace& trace)
{
    if(tracedata.mean_stop_index >= tracedata.mean_start_index)
    {
        trace.mean_start_index = tracedata.mean_start_index;
        trace.mean_stop_index = tracedata.mean_stop_index;
        double tempmean = UTL::ZERO_DOUBLE;
        for(auto i = trace.mean_start_index; i < trace.mean_stop_index; ++i)
        {
            tempmean += trace.value[i];
        }
        trace.mean = tempmean / (double) (trace.mean_stop_index - trace.mean_start_index);
    }
}
//____________________________________________________________________________________________
bool liberallrfInterface::updatePHIDEG()
{   // ONLY ever called from staticEntryLLRFMonitor
    double val = (llrf.phi_ff) * (llrf.phiCalibration);
    debugMessage("setPHIDEG PHI_DEG to, ",val, ", calibration = ", llrf.phiCalibration);
    return setValue2(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::PHI_DEG),val);
}
//____________________________________________________________________________________________
bool liberallrfInterface::updateAMPMVM()
{   // ONLY ever called from staticEntryLLRFMonitor
    double val = (llrf.amp_ff) * (llrf.ampCalibration);
    debugMessage("setAMPMVM AMP_MVM to, ",val, ", calibration = ", llrf.ampCalibration);
    return setValue2(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::AMP_MVM),val);
}

//____________________________________________________________________________________________
void liberallrfInterface::updateEVID(const event_handler_args& args,llrfStructs::rf_trace_data& trace)
{
    llrfStructs::rf_trace &t = trace.traces[trace.evid_current_trace];
    //debugMessage("updateEVID START, trace.name = ", trace.name);
    if(isTimeType(args.type))
    {
        const dbr_time_string* p = nullptr;
        p = (const struct dbr_time_string*)args.dbr;
        //message("m1a");

        if(p != nullptr)
        {
            //message("m1aa");
            if((*p).value == '\0')
            {
                //message("m1a");
                t.EVID = "";
                //trace.traces[trace.evid_current_trace].EVID = "";
                message("null string passsed back by EPICS!!!!");
            }
            else
            {
                t.EVID = p->value;
                //message("t.EVID ", t.EVID);

                t.EVID_etime = p->stamp;
                updateTime(t.EVID_etime,
                       t.EVID_time,
                       t.EVID_timeStr);
                //message("t.EVID ", t.EVID, " t.EVID_timeStr ", t.EVID_timeStr);

            }
        }
        else
        {
            t.EVID = "nullptr";
        }
    }
    else
    {
        t.EVID = *((std::string*)args.dbr);
    }
    // update evid_current_trace index and roll back to beginning
    //
    //
    // the trace index tells us which part of 'traces' is the next to update
    // it circles 'round
    // EVID has a different counter than the llrf traces
    // This means it can get de-synchronised with the llrf traces
    // We may jsut have to bite the bullet on this one until we get Beam-synchronous data
    // in EPICS 4
    updateTraceIndex(trace.evid_current_trace , trace.traces.size());
    updateTraceIndex(trace.previous_evid_trace, trace.traces.size());
    trace.traces[trace.evid_current_trace].EVID = "NOT_SET"; //MAGIC_STRING

//    // active pulses are counted from
//    if(UTL::KLYSTRON_FORWARD_POWER == trace.name)
//    {
//        updateActivePulseCount(t.EVID);
//    }


    // debugMessage("NEW trace.evid_current_trace  = ", trace.evid_current_trace, " ", t.EVID);
    // debugMessage("updateEVID FIN ");
}

//____________________________________________________________________________________________
void liberallrfInterface::updateSCAN(const event_handler_args& args,llrfStructs::rf_trace_data& trace)
{
    switch(*(int*)args.dbr)
    {
        case 0:
            trace.scan = llrfStructs::LLRF_SCAN::PASSIVE;
            break;
        case 1:
            trace.scan = llrfStructs::LLRF_SCAN::EVENT;
            break;
        case 2:
            trace.scan = llrfStructs::LLRF_SCAN::IO_INTR;
            break;
        case 3:
            trace.scan = llrfStructs::LLRF_SCAN::TEN;
            break;
        case 4:
            trace.scan = llrfStructs::LLRF_SCAN::FIVE;
            break;
        case 5:
            trace.scan = llrfStructs::LLRF_SCAN::TWO;
            break;
        case 6:
            trace.scan = llrfStructs::LLRF_SCAN::ONE;
            break;
        case 7:
            trace.scan = llrfStructs::LLRF_SCAN::ZERO_POINT_FIVE;
            break;
        case 8:
            trace.scan = llrfStructs::LLRF_SCAN::ZERO_POINT_TWO;
            break;
        case 9:
            trace.scan = llrfStructs::LLRF_SCAN::ZERO_POINT_ONE;
            break;
        default:
            trace.scan = llrfStructs::LLRF_SCAN::UNKNOWN_SCAN;
    }
    //message("New SCAN for trace ", trace.name, " = ", ENUM_TO_STRING(trace.scan));
}
//____________________________________________________________________________________________
void liberallrfInterface::updateTrigState(const event_handler_args& args)
{   /* manually figure out which int state corresponds to my enum */
    switch( *(int*)args.dbr)
    {
        case 0:
            llrf.trig_source = llrfStructs::TRIG::OFF;
            break;
        case 1:
            llrf.trig_source = llrfStructs::TRIG::EXTERNAL;
            break;
        case 2:
            llrf.trig_source = llrfStructs::TRIG::INTERNAL;
            break;
    }
}
void liberallrfInterface::set_evid_ID_SET(llrfStructs::rf_trace_data& trace)
{
    if(evid_ID_SET)
    {

    }
    else
    {
        if(trace.traces[trace.evid_current_trace].EVID == "NOT_SET")//MAGIC_STRING
        {
            evid_id  = getSize(trace.traces[trace.previous_evid_trace].EVID) +1;
            evid_ID_SET = true;
        }
        else
        {
            evid_id  = getSize(trace.traces[trace.previous_evid_trace].EVID);
            evid_ID_SET = true;
        }
    }
}

//____________________________________________________________________________________________
//void liberallrfInterface::calcStandardDeviation(llrfStructs::rf_trace_data& trace)
//{
//    //https://stackoverflow.com/questions/7616511/calculate-mean-and-standard-deviation-from-a-vector-of-samples-in-c-using-boos
//
//    double sum = std::accumulate(std::begin(v), std::end(v), 0.0);
//double m =  sum / v.size();
//
//double accum = 0.0;
//std::for_each (std::begin(v), std::end(v), [&](const double d) {
//    accum += (d - m) * (d - m);
//});
//
//double stdev = sqrt(accum / (v.size()-1));
//
//}
//____________________________________________________________________________________________
void liberallrfInterface::updateTraceIndex(size_t& index, const  size_t trace_size)
{
    index += 1;
    if(index == trace_size)
    {
        index = 0;
    }
}
//____________________________________________________________________________________________
void liberallrfInterface::updateTraceIndex(int & index, const size_t trace_size)
{
    index += 1;
    if(index == (int)trace_size)
    {
        index = 0;
    }
}
// below not use anymore
//____________________________________________________________________________________________
//bool liberallrfInterface::shouldSubtractTraceFromRollingAverage(llrfStructs::rf_trace_data& trace)
//{
//    // we only subtract AFTER rolling_sum_counter is larger than average_size
//    if(trace.rolling_sum_counter > trace.average_size)
//        // paranoid sanity check
//        if(trace.average_trace_values.size() >= trace.average_size)
//        {
//            //debugMessage("shoudlSubtractTraceFromRollingAverage = true");
//            return true;
//        }
//    //debugMessage("shoudlSubtractTraceFromRollingAverage = false");
//    return false;
//}
//____________________________________________________________________________________________
bool liberallrfInterface::Is_TracePV(llrfStructs::LLRF_PV_TYPE pv)
{
    bool r = false;
//    if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM)
//    {
//        r = true;
//    }
    return r;
}
//____________________________________________________________________________________________
bool liberallrfInterface::IsNot_EVID_PV(llrfStructs::LLRF_PV_TYPE pv)
{
    return !Is_EVID_PV(pv);
}
//____________________________________________________________________________________________
bool liberallrfInterface::Is_EVID_PV(llrfStructs::LLRF_PV_TYPE pv)
{
    bool r = false;
//    if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM_EVID)
//    {
//        r = true;
//    }
//    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM_EVID)
//    {
//        r = true;
//    }
    return r;
}
//____________________________________________________________________________________________
void liberallrfInterface::startTimer()
{
    llrf.timer_start = msChronoTime();
}
//____________________________________________________________________________________________
long long liberallrfInterface::elapsedTime()
{
    return msChronoTime() - llrf.timer_start;
}
//____________________________________________________________________________________________
bool liberallrfInterface::Is_Time_Vector_PV(llrfStructs::LLRF_PV_TYPE pv)
{
    if(pv == llrfStructs::LLRF_PV_TYPE::LIB_TIME_VECTOR)
        return true;
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::IsNot_TracePV(llrfStructs::LLRF_PV_TYPE pv)
{
    return !Is_TracePV(pv);
}
//____________________________________________________________________________________________
bool liberallrfInterface::isMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    bool r = false;
    for(auto && it : continuousMonitorStructs)
    {
        if(it->monType == pv)
        {
            r = true;
            break;
        }
    }
    return r;
}
//____________________________________________________________________________________________
bool liberallrfInterface::isNotMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    return !isMonitoring(pv);
}
//____________________________________________________________________________________________
bool liberallrfInterface::isMonitoring(const std::string& name)
{
    return isMonitoring(getLLRFPVType(name));
}
//____________________________________________________________________________________________
bool liberallrfInterface::isNotMonitoring(const std::string& name)
{
    return isNotMonitoring(getLLRFPVType(name));
}


//
//
//
//   _________.__               .__             _________       __    __
//  /   _____/|__| _____ ______ |  |   ____    /   _____/ _____/  |__/  |_  ___________  ______
//  \_____  \ |  |/     \\____ \|  | _/ __ \   \_____  \_/ __ \   __\   __\/ __ \_  __ \/  ___/
//  /        \|  |  Y Y  \  |_> >  |_\  ___/   /        \  ___/|  |  |  | \  ___/|  | \/\___ \
// /_______  /|__|__|_|  /   __/|____/\___  > /_______  /\___  >__|  |__|  \___  >__|  /____  >
//         \/          \/|__|             \/          \/     \/                \/           \/
//
// i.e. to be exposed to python
//____________________________________________________________________________________________
//____________________________________________________________________________________________
bool liberallrfInterface::disableInfiniteMaskEndByPower(const std::string& phase_trace)
{
    const std::string phase = fullCavityTraceName(phase_trace);
    if(entryExists(llrf.trace_data,phase))
    {
        llrf.trace_data.at(phase).endInfiniteMask_Trace_Set = false;
        return true;
    }
    return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setInfiniteMaskEndByPower(const std::string& power_trace,const std::string& phase_trace,const double level)
{
    if(stringIsSubString(power_trace,UTL::POWER))
    {
        if(stringIsSubString(phase_trace,UTL::PHASE))
        {
            const std::string power = fullCavityTraceName(power_trace);
            const std::string phase = fullCavityTraceName(phase_trace);

            if(entryExists(llrf.trace_data,power))
            {
                if(entryExists(llrf.trace_data,phase))
                {
                    llrf.trace_data.at(phase).endMaskTrace_Bound.first  = power;
                    llrf.trace_data.at(phase).endMaskTrace_Bound.second = level;
                    llrf.trace_data.at(phase).endInfiniteMask_Trace_Set = true;
                    message(llrf.trace_data.at(phase).name, "endInfiniteMask_Trace_Set = true");
                    return true;
                }
                else
                {
                    llrf.trace_data.at(phase).endInfiniteMask_Trace_Set = false;
                }
            }
        }
    }
    return false;
}

//____________________________________________________________________________________________
bool liberallrfInterface::trigOff()
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::TRIG_SOURCE),llrfStructs::TRIG::OFF);
}
//____________________________________________________________________________________________
bool liberallrfInterface::trigInt()
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::TRIG_SOURCE),llrfStructs::TRIG::INTERNAL);
}
//____________________________________________________________________________________________
bool liberallrfInterface::trigExt()
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::TRIG_SOURCE),llrfStructs::TRIG::EXTERNAL);
}

////____________________________________________________________________________________________
//bool liberallrfInterface::setNumContinuousOutsideMaskCount(const std::string& name, const size_t val)
//{   // number of continuous points outside_mask to register as outside_mask
//    const std::string n = fullCavityTraceName(name);
//    if(entryExists(llrf.trace_data, n))
//    {
//        llrf.trace_data.at(n).num_continuous_outside_mask_count = val;
//        return true;
//    }
//    return false;
//}
//____________________________________________________________________________________________
void liberallrfInterface::offsetTimer(long long value)
{   // add an ofset to the internal timer
    llrf.timer_start += value;
}
//____________________________________________________________________________________________
//____________________________________________________________________________________________
bool liberallrfInterface::setPulseLength(double value)
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_PULSE_LENGTH),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setPulseOffset(double value)
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_PULSE_OFFSET),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setPhiSP(double value)
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_PHI_SP),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setPhiFF(double value)
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_PHI_FF),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAmpSP(double value)
{
    if(value>llrf.maxAmp)
    {
        message("Error!! Requested amplitude, ",value,"  too high");
        return false;
    }
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_AMP_SP),value);
}

bool liberallrfInterface::resetToValue(const double value)
{
   // message("resetToValue Start");
    bool r = setAmpSP(value);
    if( r == false )
    {
        message("resetToValue failed to send amp SP value");
        return false;
    }

    pause_1();
    //message("resetToValue setInterlockNonActive");

    r = setInterlockNonActive();
    if( r == false )
    {
        message("resetToValue failed to setInterlockNonActive");
        return false;
    }

    pause_2();
    //message("resetToValue enableRFOutput");

    r = enableRFOutput();
    if( r == false )
    {
        message("resetToValue failed to enableRFOutput");
        return false;
    }

    pause_2();
    //message("resetToValue lockPhaseFF");

    r = lockPhaseFF();
    if( r == false )
    {
        message("resetToValue failed to lockPhaseFF");
        return false;
    }
    pause_2();
    pause_2();
    //message("resetToValue lockAmpFF");

    r = lockAmpFF();
    if( r == false )
    {
        message("resetToValue failed to lockAmpFF");
        return false;
    }
    //message("resetToValue Returned True");
    return r;
}



//____________________________________________________________________________________________
bool liberallrfInterface::setAmpFF(double value)
{
    if(value>llrf.maxAmp)
    {
        message("Error!! Requested amplitude, ",value,"  too high");
        return false;
    }
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_AMP_FF),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAmpLLRF(double value)
{
    return setAmpSP(value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setPhiLLRF(double value)
{
    return setPhiSP(value);
}
//____________________________________________________________________________________________
void liberallrfInterface::setPhiCalibration(double value)
{
    llrf.phiCalibration = value;
}
//____________________________________________________________________________________________
void liberallrfInterface::setAmpCalibration(double value)
{
    llrf.ampCalibration = value;
}

//____________________________________________________________________________________________
void liberallrfInterface::setCrestPhiLLRF(double value) // in LLRF units
{
    llrf.crestPhi = value;
}


//____________________________________________________________________________________________
bool liberallrfInterface::setAmpMVM(double value)// MV / m amplitude
{
    bool r = false;
    if(value<UTL::ZERO_DOUBLE)
    {
        message("Error!! you must set a positive amplitude for LLRF, not ", value);
    }
    else
    {
        //double val = std::round(value/llrf.ampCalibration);
        double val = value/llrf.ampCalibration;
        if(val > llrf.maxAmp)
        {
            message("Error!! Requested amplitude, ",val,"  too high");
        }
        else
        {
            debugMessage("Requested amplitude, ", value," MV/m = ", val," in LLRF units ");
            r = setAmpLLRF(val);
        }
    }
    return r;
}
//____________________________________________________________________________________________
bool liberallrfInterface::setPhiDEG(double value)// degrees relative to crest
{
    bool r = false;
    if(value<-180.0||value>180.0)//MAGIC_NUMBER
    {
        message("Error!! you must set phase between -180.0 and +180.0, not ", value);
    }
    else
    {
        double val = llrf.crestPhi + value/llrf.phiCalibration;

        debugMessage("Requested PHI, ", value," degrees  = ", val," in LLRF units ");
        r = setPhiLLRF(val);
    }
    return r;
}


//____________________________________________________________________________________________
bool liberallrfInterface::setPercentTimeMask(const double s1,const double s2,const double s3,
                                         const double s4,const double value2,const  std::string& name)
{
    // The mask is defined in terms of time, using the time_vector to find the indece
    return setPercentMask(getIndex(s1),getIndex(s2),getIndex(s3),getIndex(s4),value2,name);
}
//____________________________________________________________________________________________
/// THE NEXT TWO FUNCTIONS COULD BE COMBINED AND NEATENED UP
//____________________________________________________________________________________________
bool liberallrfInterface::setPercentMask(const size_t s1,const size_t s2,const size_t s3,
                                         const size_t s4,const double value,const  std::string& name)
{
    return set_mask(s1,s2,s3,s4,value/100.0,name,true);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAbsoluteTimeMask(const double s1,const double s2,const double s3,
                                         const double s4,const double value,const  std::string& name)
{
    // The mask is defined in terms of time, using the time_vector to find the indece
    return setAbsoluteMask(getIndex(s1),getIndex(s2),getIndex(s3),getIndex(s4),value,name);
}

//____________________________________________________________________________________________
bool liberallrfInterface::setAbsoluteMask(const size_t s1,const size_t s2,
                                          const size_t s3,const size_t s4,
                                          const double value,const  std::string& name)
{
    return set_mask(s1,s2,s3,s4,value,name,false);
}


//____________________________________________________________________________________________
bool liberallrfInterface::setMaskInfiniteEnd(const std::string& trace_name, size_t index)
{
    if(index + 1 < llrf.trace_data.at(trace_name).low_mask.size())
    {
        //assume entry eixtsts
        llrf.trace_data.at(trace_name).mask_end_by_power_index = index;
        auto lo_it = llrf.trace_data.at(trace_name).low_mask.begin();
        std::advance(lo_it, index);
        auto hi_it = llrf.trace_data.at(trace_name).high_mask.begin();
        std::advance(hi_it, index);
        for(;lo_it != llrf.trace_data.at(trace_name).low_mask.end() && hi_it != llrf.trace_data.at(trace_name).high_mask.end(); ++lo_it,++hi_it)
        {
            *lo_it = -std::numeric_limits<double>::infinity();
            *hi_it =  std::numeric_limits<double>::infinity();
        }
    }
}
//____________________________________________________________________________________________
size_t liberallrfInterface::getMaskInfiniteEndByPowerIndex(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        //assume entry eixtsts
        return llrf.trace_data.at(n).mask_end_by_power_index;
    }
    return 0;
}
//____________________________________________________________________________________________
double liberallrfInterface::getMaskInfiniteEndByPowerTime(const std::string& name)
{
    return getTime(getMaskInfiniteEndByPowerIndex(name));
}
//____________________________________________________________________________________________
//____________________________________________________________________________________________
bool liberallrfInterface::setCavRevPwrHiMask(const std::vector<double>& value)
{
    return setHiMask(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setCavRevPwrLoMask(const std::vector<double>& value)
{
    return setLoMask(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setCavRevPwrMaskPercent(const size_t s1,const size_t s2,const size_t s3,const size_t s4,const double value)
{
    return setPercentMask(s1,s2,s3,s4,value,fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
}
//____________________________________________________________________________________________
bool liberallrfInterface::setCavRevPwrMaskAbsolute(const size_t s1,const size_t s2,const size_t s3,const size_t s4,const double value)
{
    return setAbsoluteMask(s1,s2,s3,s4,value,fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
}
//____________________________________________________________________________________________
bool liberallrfInterface::setCavFwdPwrMaskPercent(const size_t s1,const size_t s2,const size_t s3,const size_t s4,const double value)
{
    return setPercentMask(s1,s2,s3,s4,value,fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
}
//____________________________________________________________________________________________
bool liberallrfInterface::setCavFwdPwrMaskAbsolute(const size_t s1,const size_t s2,const size_t s3,const size_t s4,const double value)
{
    return setAbsoluteMask(s1,s2,s3,s4,value,fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
}

//____________________________________________________________________________________________

//____________________________________________________________________________________________

//_____________________________________________________________________________________________________
//_____________________________________________________________________________________________________
//
//
// .___        __                             .__      _________       __    __
// |   | _____/  |_  ___________  ____ _____  |  |    /   _____/ _____/  |__/  |_  ___________  ______
// |   |/    \   __\/ __ \_  __ \/    \\__  \ |  |    \_____  \_/ __ \   __\   __\/ __ \_  __ \/  ___/
// |   |   |  \  | \  ___/|  | \/   |  \/ __ \|  |__  /        \  ___/|  |  |  | \  ___/|  | \/\___ \
// |___|___|  /__|  \___  >__|  |___|  (____  /____/ /_______  /\___  >__|  |__|  \___  >__|  /____  >
//          \/          \/           \/     \/               \/     \/                \/           \/
//
// i.e. things used by functions in this class, not primarily to be exposed to python
//_____________________________________________________________________________________________________
template<typename T>
bool liberallrfInterface::setValue(llrfStructs::pvStruct& pvs, T value)
{
    std::stringstream ss;
    ss << "setValue setting " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    bool ret = false;
    ca_put(pvs.CHTYPE, pvs.CHID, &value);
    //debugMessage(ss);
    ss.str("");
    ss << "Timeout setting llrf, " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    int status = sendToEpics("ca_put","",ss.str().c_str());
    if(status==ECA_NORMAL)
        ret=true;
    return ret;
}
//____________________________________________________________________________________________
template<typename T>
bool liberallrfInterface::setValue2(llrfStructs::pvStruct& pvs, T value)
{
    bool ret = false;
    ca_put(pvs.CHTYPE,pvs.CHID,&value);
    std::stringstream ss;
    ss << "setValue2 setting " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    debugMessage(ss);
    ss.str("");
    ss << "Timeout setting llrf, " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    int status = sendToEpics2("ca_put","",ss.str().c_str());
    if(status==ECA_NORMAL)
        ret=true;
    return ret;
}
//
//
//
// .___        __                             .__      ________        __    __
// |   | _____/  |_  ___________  ____ _____  |  |    /  _____/  _____/  |__/  |_  ___________  ______
// |   |/    \   __\/ __ \_  __ \/    \\__  \ |  |   /   \  ____/ __ \   __\   __\/ __ \_  __ \/  ___/
// |   |   |  \  | \  ___/|  | \/   |  \/ __ \|  |__ \    \_\  \  ___/|  |  |  | \  ___/|  | \/\___ \
// |___|___|  /__|  \___  >__|  |___|  (____  /____/  \______  /\___  >__|  |__|  \___  >__|  /____  >
//          \/          \/           \/     \/               \/     \/                \/           \/
//
//  i.e. things used by functions in this class, not primarily to be exposed to python
//____________________________________________________________________________________________
std::vector<std::string> liberallrfInterface::getChannelNames()
{
    std::vector<std::string>  r;
    for(auto && it:llrf.pvMonStructs)
    {
        if(Is_TracePV(it.first))
        {
            r.push_back(it.second.name);
        }
    }
    return r;
}
//____________________________________________________________________________________________
std::vector<std::string> liberallrfInterface::getTraceNames()
{
    std::vector<std::string>  r;
    for(auto && it:llrf.trace_data)
    {
        r.push_back(it.first);
    }
    return r;
}
//____________________________________________________________________________________________
llrfStructs::LLRF_PV_TYPE liberallrfInterface::getLLRFPVType(const std::string& name)
{
    std::string n = fullCavityTraceName(name);
    for(auto && it:llrf.pvMonStructs)
    {
        if(it.second.name == n)
        {
            return it.first;
        }
    }
    return llrfStructs::LLRF_PV_TYPE::UNKNOWN;
}
//____________________________________________________________________________________________
std::string liberallrfInterface::getLLRFChannelName(const llrfStructs::LLRF_PV_TYPE pv)
{
    for(auto && it:llrf.pvMonStructs)
    {
        if(it.first == pv)
        {
            return it.second.name;
        }
    }
    return ENUM_TO_STRING(llrfStructs::LLRF_PV_TYPE::UNKNOWN);
}
//____________________________________________________________________________________________
std::string liberallrfInterface::fullCavityTraceName(const std::string& name)const
{
    if(myLLRFType == llrfStructs::LLRF_TYPE::CLARA_HRRG || myLLRFType == llrfStructs::LLRF_TYPE::VELA_HRRG)
    {
        if(name == UTL::CAVITY_REVERSE_PHASE)
            return UTL::HRRG_CAVITY_REVERSE_PHASE;

        if(name == UTL::CAVITY_FORWARD_PHASE)
            return UTL::HRRG_CAVITY_FORWARD_PHASE;

        if(name == UTL::CAVITY_REVERSE_POWER)
            return UTL::HRRG_CAVITY_REVERSE_POWER;

        if(name == UTL::CAVITY_FORWARD_POWER)
            return UTL::HRRG_CAVITY_FORWARD_POWER;
    }
    else if(myLLRFType == llrfStructs::LLRF_TYPE::CLARA_LRRG || myLLRFType == llrfStructs::LLRF_TYPE::VELA_LRRG)
    {
        if(name == UTL::CAVITY_REVERSE_PHASE)
            return UTL::LRRG_CAVITY_REVERSE_PHASE;

        if(name == UTL::CAVITY_FORWARD_PHASE)
            return UTL::LRRG_CAVITY_FORWARD_PHASE;

        if(name == UTL::CAVITY_REVERSE_POWER)
            return UTL::LRRG_CAVITY_REVERSE_POWER;

        if(name == UTL::CAVITY_FORWARD_POWER)
            return UTL::LRRG_CAVITY_FORWARD_POWER;
    }
    return name;
}




//____________________________________________________________________________________________
llrfStructs::LLRF_PV_TYPE liberallrfInterface::getEVID_pv(llrfStructs::LLRF_PV_TYPE pv)
{
    switch(pv)
    {
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM_EVID;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM_EVID;
    }
    return llrfStructs::LLRF_PV_TYPE::UNKNOWN;
}
//____________________________________________________________________________________________
//llrfStructs::LLRF_PV_TYPE liberallrfInterface::getSCAN_pv(llrfStructs::LLRF_PV_TYPE pv)
//{
//    switch(pv)
//    {
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH1_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH2_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH3_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH4_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH5_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH6_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH7_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH8_PWR_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH1_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH2_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH3_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH4_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH5_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH6_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH7_PHASE_REM_SCAN;
//         case llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM:
//             return llrfStructs::LLRF_PV_TYPE::LIB_CH8_PHASE_REM_SCAN;
//    }
//    return llrfStructs::LLRF_PV_TYPE::UNKNOWN;
//}
//____________________________________________________________________________________________
//
//
//
//   _________.__               .__             ________        __    __
//  /   _____/|__| _____ ______ |  |   ____    /  _____/  _____/  |__/  |_  ___________  ______
//  \_____  \ |  |/     \\____ \|  | _/ __ \  /   \  ____/ __ \   __\   __\/ __ \_  __ \/  ___/
//  /        \|  |  Y Y  \  |_> >  |_\  ___/  \    \_\  \  ___/|  |  |  | \  ___/|  | \/\___ \
// /_______  /|__|__|_|  /   __/|____/\___  >  \______  /\___  >__|  |__|  \___  >__|  /____  >
//         \/          \/|__|             \/          \/     \/                \/           \/
//
// i.e. things to expose to python
//____________________________________________________________________________________________


std::vector<double> liberallrfInterface::getTimeVector()const
{
    return llrf.time_vector;
}

//____________________________________________________________________________________________
double liberallrfInterface::getPhiCalibration()
{
    return llrf.phiCalibration;
}
//____________________________________________________________________________________________
double liberallrfInterface::getAmpCalibration()
{
    return llrf.ampCalibration;
}
//____________________________________________________________________________________________
double liberallrfInterface::getCrestPhiLLRF()// in LLRF units
{
    return llrf.crestPhi;
}
//____________________________________________________________________________________________
double liberallrfInterface::getAmpFF()
{
    return llrf.amp_ff;
}
//____________________________________________________________________________________________
double liberallrfInterface::getAmpSP()
{
    return llrf.amp_sp;
}
//____________________________________________________________________________________________
double liberallrfInterface::getAmpMVM()// physics units
{
    return llrf.amp_MVM;
}
//____________________________________________________________________________________________
double liberallrfInterface::getAmpLLRF()// physics units
{
    return getAmpFF();
}
//____________________________________________________________________________________________
double liberallrfInterface::getPhiFF()
{
    return llrf.phi_ff;
}
//____________________________________________________________________________________________
double liberallrfInterface::getPhiSP()
{
    return llrf.phi_sp;
}
//____________________________________________________________________________________________
double liberallrfInterface::getPhiDEG()// physics units
{
    return llrf.phi_DEG;
}
//____________________________________________________________________________________________
double liberallrfInterface::getPhiLLRF()// physics units
{
    return getPhiFF();
}
//____________________________________________________________________________________________
double liberallrfInterface::getPulseLength()
{
    return llrf.pulse_length;
}
//____________________________________________________________________________________________
double liberallrfInterface::getPulseOffset()
{
    return llrf.pulse_offset;
}
//____________________________________________________________________________________________
size_t liberallrfInterface::getShotCount(const std::string& name)
{
    std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        return llrf.trace_data.at(n).shot;
    }
    else
        message("liberallrfInterface::getShotCount ERROR, trace ", n, " does not exist");
    return UTL::ZERO_SIZET;
}
//____________________________________________________________________________________________
double liberallrfInterface::getBreakDownRate()
{
    return llrf.breakdown_rate;
}
//____________________________________________________________________________________________
double liberallrfInterface::getTime(const size_t index)
{
    return llrf.time_vector[index];
}
//____________________________________________________________________________________________
const llrfStructs::liberallrfObject& liberallrfInterface::getLLRFObjConstRef()
{
    return llrf;
}
//____________________________________________________________________________________________
const llrfStructs::rf_trace_data& liberallrfInterface::getTraceDataConstRef(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        return llrf.trace_data.at(n);
    }
    else
    {
        message("liberallrfInterface::getTraceDataConstRef() ERROR, trace ", n, " does not exist");

    }
    return dummy_trace_data;
}

//____________________________________________________________________________________________

//____________________________________________________________________________________________
llrfStructs::LLRF_TYPE liberallrfInterface::getType()
{
    return llrf.type;
}
//____________________________________________________________________________________________
size_t liberallrfInterface::getTraceLength()
{
    return llrf.traceLength;
}

//____________________________________________________________________________________________
llrfStructs::TRIG liberallrfInterface::getTrigSource()
{
    return llrf.trig_source;
}
//____________________________________________________________________________________________
size_t liberallrfInterface::getNumBufferTraces(const std::string&name)
{
        const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data, n))
    {
        return llrf.trace_data.at(n).buffersize;
    }
    else
        message("liberallrfInterface::getNumBufferTraces ERROR, trace ", n, " does not exist");
    return UTL::ZERO_SIZET;
}
//____________________________________________________________________________________________
//size_t liberallrfInterface::getNumRollingAverageTraces(const std::string&name)
//{
//    const std::string n = fullCavityTraceName(name);
//    if(entryExists(llrf.trace_data,n))
//    {
//        return llrf.trace_data.at(n).average_size;
//    }
//    else
//        message("liberallrfInterface::getNumRollingAverageTraces ERROR, trace ", n, " does not exist");
//    return UTL::ZERO_SIZET;
//}
//
// GET LATEST TRACE VALUES

//
// GET LATEST TRACE DATA STRUCT
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getTraceData(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data,n))
    {
        //return llrf.trace_data.at(name).traces.back();
        return llrf.trace_data.at(n).traces[llrf.trace_data.at(name).latest_trace_index];
    }
    else
        message("liberallrfInterface::getTraceData ERROR, trace ", n, " does not exist");
    llrfStructs::rf_trace r;
    return r;
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getCavRevPowerData()
{
    return getTraceData(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getCavRevPhaseData()
{
    return getTraceData(fullCavityTraceName(UTL::CAVITY_REVERSE_PHASE));
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getCavFwdPowerData()
{
    return getTraceData(fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getCavFwdPhaseData()
{
    return getTraceData(fullCavityTraceName(UTL::CAVITY_FORWARD_PHASE));
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getKlyRevPowerData()
{
    return getTraceData(UTL::KLYSTRON_REVERSE_POWER);
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getKlyRevPhaseData()
{
    return getTraceData(UTL::KLYSTRON_REVERSE_PHASE);
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getKlyFwdPowerData()
{
    return getTraceData(UTL::KLYSTRON_FORWARD_POWER);
}
//____________________________________________________________________________________________
llrfStructs::rf_trace liberallrfInterface::getKlyFwdPhaseData()
{
    return getTraceData(UTL::KLYSTRON_FORWARD_PHASE);
}
//
// GET THE ENTIRE TRACE DATA BUFFER
//
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getTraceBuffer(const std::string& name)
{
        const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data,n))
    {
        return llrf.trace_data.at(n).traces;
    }
    else
        message("liberallrfInterface::getTraceBuffer ERROR, trace ", n, " does not exist");
    std::vector<llrfStructs::rf_trace> r;
    return r;
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getCavRevPowerBuffer()
{
    return getTraceBuffer(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getCavRevPhaseBuffer()
{
    return getTraceBuffer(fullCavityTraceName(UTL::CAVITY_REVERSE_PHASE));
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getCavFwdPowerBuffer()
{
    return getTraceBuffer(fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getCavFwdPhaseBuffer()
{
    return getTraceBuffer(fullCavityTraceName(UTL::CAVITY_FORWARD_PHASE));
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getKlyRevPowerBuffer()
{
    return getTraceBuffer(UTL::KLYSTRON_REVERSE_POWER);
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getKlyRevPhaseBuffer()
{
    return getTraceBuffer(UTL::KLYSTRON_REVERSE_PHASE);
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getKlyFwdPowerBuffer()
{
    return getTraceBuffer(UTL::KLYSTRON_FORWARD_POWER);
}
//____________________________________________________________________________________________
std::vector<llrfStructs::rf_trace> liberallrfInterface::getKlyFwdPhaseBuffer()
{
    return getTraceBuffer(UTL::KLYSTRON_REVERSE_PHASE);
}
//
// GET TRACE AVERAGES
//
//____________________________________________________________________________________________
std::vector<double>liberallrfInterface::getAverageTraceData(const std::string& name)
{
    const std::string n = fullCavityTraceName(name);
    if(entryExists(llrf.trace_data,n))
    {
        if(llrf.trace_data.at(n).has_average)
        {
            //return llrf.trace_data.at(name).traces.back();
            return llrf.trace_data.at(n).rolling_average;
        }
    }
    else
        message("liberallrfInterface::getAverageTraceData ERROR, trace ", n, " does not exist");
    std::vector<double> r;
    return r;
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavRevPowerAv()
{
    return getAverageTraceData(UTL::CAVITY_REVERSE_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavFwdPowerAv()
{
    return getAverageTraceData(UTL::CAVITY_FORWARD_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyRevPowerAv()
{
    return getAverageTraceData(UTL::KLYSTRON_REVERSE_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyFwdPowerAv()
{
    return getAverageTraceData(UTL::KLYSTRON_FORWARD_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavRevPhaseAv()
{
    return getAverageTraceData(UTL::CAVITY_REVERSE_PHASE);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavFwdPhaseAv()
{
    return getAverageTraceData(UTL::CAVITY_FORWARD_PHASE);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyRevPhaseAv()
{
    return getAverageTraceData(UTL::KLYSTRON_REVERSE_PHASE);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyFwdPhaseAv()
{
    return getAverageTraceData(UTL::KLYSTRON_FORWARD_PHASE);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getProbePowerAv()
{
    return getAverageTraceData(UTL::CAVITY_PROBE_POWER);
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getProbePhaseAv()
{
    return getAverageTraceData(UTL::CAVITY_PROBE_PHASE);
}
//____________________________________________________________________________________________
//
//
// ___________                               _____                .__  __
// \__    ___/___________    ____  ____     /     \   ____   ____ |__|/  |_  ___________  ______
//   |    |  \_  __ \__  \ _/ ___\/ __ \   /  \ /  \ /  _ \ /    \|  \   __\/  _ \_  __ \/  ___/
//   |    |   |  | \// __ \\  \__\  ___/  /    Y    ( <_>)   |  \  ||  | ( <_>)  | \/\___ \
//   |____|   |__|  (____  /\___  >___  > \____|__  /\____/|___|  /__||__|  \____/|__|  /____  >
//                       \/     \/    \/          \/            \/                           \/
//
//____________________________________________________________________________________________
bool liberallrfInterface::startCavFwdTraceMonitor()
{
    bool a1 = startTraceMonitoring(fullCavityTraceName(UTL::CAVITY_FORWARD_PHASE));
    bool a2 = startTraceMonitoring(fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::startCavRevTraceMonitor()
{
    bool a1 = startTraceMonitoring(fullCavityTraceName(UTL::CAVITY_REVERSE_PHASE));
    bool a2 = startTraceMonitoring(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::startKlyFwdTraceMonitor()
{
    bool a1 = startTraceMonitoring(UTL::KLYSTRON_FORWARD_PHASE);
    bool a2 = startTraceMonitoring(UTL::KLYSTRON_FORWARD_POWER);
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::startKlyRevTraceMonitor()
{
    bool a1 = startTraceMonitoring(UTL::KLYSTRON_REVERSE_PHASE);
    bool a2 = startTraceMonitoring(UTL::KLYSTRON_REVERSE_POWER);
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopTraceMonitoring(const std::string& name)
{
    return stopTraceMonitoring(getLLRFPVType(name));
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopTraceMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    for(auto && it : continuousMonitorStructs)
    {
        if(Is_TracePV(it->monType))
        {
            if(it->monType == pv)
            {
                killMonitor(it);
                delete it;
            }
        }
    }
    return isNotMonitoring(pv);
}
//____________________________________________________________________________________________
void liberallrfInterface::stopTraceMonitoring()
{
    for(auto && it : continuousMonitorStructs)
    {
        if(Is_TracePV(it->monType) )
        {
            killMonitor(it);
            delete it;
        }
    }
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopCavFwdTraceMonitor()
{
    bool a1 = stopTraceMonitoring(fullCavityTraceName(UTL::CAVITY_FORWARD_PHASE));
    bool a2 = stopTraceMonitoring(fullCavityTraceName(UTL::CAVITY_FORWARD_POWER));
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopCavRevTraceMonitor()
{
    bool a1 = stopTraceMonitoring(fullCavityTraceName(UTL::CAVITY_REVERSE_PHASE));
    bool a2 = stopTraceMonitoring(fullCavityTraceName(UTL::CAVITY_REVERSE_POWER));
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopKlyFwdTraceMonitor()
{
    bool a1 = stopTraceMonitoring(UTL::KLYSTRON_FORWARD_PHASE);
    bool a2 = stopTraceMonitoring(UTL::KLYSTRON_FORWARD_POWER);
    if(a1 && a2)
        return true;
    else
        return false;
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopKlyRevTraceMonitor()
{
    bool a1 = stopTraceMonitoring(UTL::KLYSTRON_REVERSE_PHASE);
    bool a2 = stopTraceMonitoring(UTL::KLYSTRON_REVERSE_POWER);
    if(a1 && a2)
        return true;
    else
        return false;
}












