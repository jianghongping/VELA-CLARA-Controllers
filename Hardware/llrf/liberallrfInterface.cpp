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

#include "liberallrfInterface.h"
//djs
#include "dburt.h"
#include "configDefinitions.h"
// stl
#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <thread>
#include <math.h>
//  __  ___  __   __    /  __  ___  __   __
// /  `  |  /  \ |__)  /  |  \  |  /  \ |__)
// \__,  |  \__/ |  \ /   |__/  |  \__/ |  \
//
liberallrfInterface::liberallrfInterface( const std::string &llrfConf,
                              const bool startVirtualMachine,
                              const bool* show_messages_ptr, const bool* show_debug_messages_ptr,
                              const bool shouldStartEPICs ,
                              const llrfStructs::LLRF_TYPE type
                              ):
configReader(llrfConf,startVirtualMachine,show_messages_ptr,show_debug_messages_ptr),
interface(show_messages_ptr,show_debug_messages_ptr),
shouldStartEPICs(shouldStartEPICs),
usingVirtualMachine(startVirtualMachine),
myLLRFType(type)
{
//    if( shouldStartEPICs )
//    message("magnet liberallrfInterface shouldStartEPICs is true");
//    else
//    message("magnet liberallrfInterface shouldStartEPICs is false");
    initialise();
}
//______________________________________________________________________________
liberallrfInterface::~liberallrfInterface()
{
    killILockMonitors();
    for( auto && it : continuousMonitorStructs )
    {
        killMonitor( it );
        delete it;
    }
//    debugMessage( "liberallrfInterface DESTRUCTOR COMPLETE ");
}
//______________________________________________________________________________
void liberallrfInterface::killMonitor( llrfStructs::monitorStruct * ms )
{
    int status = ca_clear_subscription( ms -> EVID );
    //if( status == ECA_NORMAL)
        //debugMessage( ms->objName, " ", ENUM_TO_STRING(ms->monType), " monitoring = false ");
//    else
        //debugMessage("ERROR liberallrfInterface: in killMonitor: ca_clear_subscription failed for ", ms->objName, " ", ENUM_TO_STRING(ms->monType) );
}
//______________________________________________________________________________
void liberallrfInterface::initialise()
{
    /// The config file reader
    configFileRead = configReader.readConfig();
    std::this_thread::sleep_for(std::chrono::milliseconds( 2000 )); // MAGIC_NUMBER
    if( configFileRead )
    {
        message("The liberallrfInterface has read the config file, acquiring objects");
        /// initialise the objects based on what is read from the config file
        bool getDataSuccess = initObjects();
        if( getDataSuccess )
        {

            if( shouldStartEPICs )
            {
                message("The liberallrfInterface has acquired objects, connecting to EPICS");
                //std::cout << "WE ARE HERE" << std::endl;
                // subscribe to the channel ids
                initChids();
                // start the monitors: set up the callback functions
                debugMessage("Starting Monitors");
                startMonitors();
                // The pause allows EPICS to catch up.
                std::this_thread::sleep_for(std::chrono::milliseconds( 2000 )); // MAGIC_NUMBER
            }
            else
             message("The liberallrfInterface has acquired objects, NOT connecting to EPICS");
        }
        else
            message( "!!!The liberallrfInterface received an Error while getting llrf data!!!" );
    }
}
//______________________________________________________________________________
bool liberallrfInterface::initObjects()
{
    bool success = configReader.getliberallrfObject(llrf);



    llrf.cav_f_power.value.resize( llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_CAV_FWD).COUNT );
    llrf.cav_r_power.value.resize( llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_CAV_REV).COUNT );
    llrf.kly_f_power.value.resize( llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_KLY_FWD).COUNT );
    llrf.kly_r_power.value.resize( llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_KLY_REV).COUNT );
    llrf.time_vector.value.resize( llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_TIME_VECTOR).COUNT );


    message( " llrf.cav_f_power.size() = ",  llrf.cav_f_power.value.size());
    message( " llrf.cav_r_power.size() = ",  llrf.cav_r_power.value.size());
    message( " llrf.kly_f_power.size() = ",  llrf.kly_f_power.value.size());
    message( " llrf.kly_r_power.size() = ",  llrf.kly_r_power.value.size());
    message( " llrf.time_vector.size() = ",  llrf.time_vector.value.size());


    llrf.type = myLLRFType;
    return success;
}
//______________________________________________________________________________
void liberallrfInterface::initChids()
{
    message( "\n", "Searching for LLRF chids...");
    for( auto && it : llrf.pvMonStructs )
    {
        addChannel( llrf.pvRoot, it.second );
    }
    // command only PVs for the LLRF to set "high level" phase and amplitide
    for( auto && it : llrf.pvComStructs )
    {
        addChannel( llrf.pvRoot, it.second );
    }
    addILockChannels( llrf.numIlocks, llrf.pvRoot, llrf.name, llrf.iLockPVStructs );
    int status=sendToEpics("ca_create_channel","Found LLRF ChIds.",
                           "!!TIMEOUT!! Not all LLRF ChIds found." );
    if(status==ECA_TIMEOUT)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));//MAGIC_NUMBER
        message("\n","Checking LLRF ChIds ");
        for( auto && it : llrf.pvMonStructs )
        {
            checkCHIDState( it.second.CHID, ENUM_TO_STRING( it.first ) );
        }
        for( auto && it : llrf.pvComStructs)
        {
            checkCHIDState( it.second.CHID, ENUM_TO_STRING( it.first ) );
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5000)); // MAGIC_NUMBER
    }
    else if ( status == ECA_NORMAL )
        allChidsInitialised = true;  // interface base class member
}
//______________________________________________________________________________
void liberallrfInterface::addChannel( const std::string & pvRoot, llrfStructs::pvStruct & pv )
{
    std::string s1;
    // TEMPORARY HACK FOR HIGH LEVEL llrf PARAMATERS
    if( pv.pvType == llrfStructs::LLRF_PV_TYPE::AMP_MVM)
    {
        s1 =  UTL::VM_PREFIX + pv.pvSuffix;
    }
    else if(  pv.pvType == llrfStructs::LLRF_PV_TYPE::PHI_DEG )
    {
        s1 =  UTL::VM_PREFIX + pv.pvSuffix;
    }
    else
    {
        s1 = pvRoot + pv.pvSuffix;
    }
    ca_create_channel( s1.c_str(), 0, 0, 0, &pv.CHID );//MAGIC_NUMBER
    debugMessage( "Create channel to ", s1 );
}
//______________________________________________________________________________
void liberallrfInterface::startMonitors()
{
    continuousMonitorStructs.clear();
    continuousILockMonitorStructs.clear();

    for( auto && it : llrf.pvMonStructs )
    {   // if using the VM we don't monito AMP Read
//        if( usingVirtualMachine && it.first == llrfStructs::LLRF_PV_TYPE::AMP_R )
//        {
//            message("For VM AMP_R is not monitored");
//        }
//        else
//        {
        if( IsNot_TracePV(it.first) )
        {
            debugMessage("ca_create_subscription to ", ENUM_TO_STRING(it.first));
            continuousMonitorStructs.push_back( new llrfStructs::monitorStruct() );
            continuousMonitorStructs.back() -> monType    = it.first;
            continuousMonitorStructs.back() -> llrfObj = &llrf;
            continuousMonitorStructs.back() -> interface  = this;
            ca_create_subscription(it.second.CHTYPE,
                                   it.second.COUNT,
                                   it.second.CHID,
                                   it.second.MASK,
                                   liberallrfInterface::staticEntryLLRFMonitor,
                                   (void*)continuousMonitorStructs.back(),
                                   &continuousMonitorStructs.back() -> EVID);
        }
    }
    int status = sendToEpics( "ca_create_subscription", "Succesfully Subscribed to LLRF Monitors", "!!TIMEOUT!! Subscription to LLRF monitors failed" );
    if ( status == ECA_NORMAL )
        allMonitorsStarted = true; /// interface base class member
}
//____________________________________________________________________________________________
bool liberallrfInterface::isMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    bool r = false;
    for( auto && it : continuousMonitorStructs )
    {
        if( it->monType == pv )
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
void liberallrfInterface::startTraceMonitoring()
{

    for( auto && it : llrf.pvMonStructs )
    {
        if( Is_TracePV(it.first) )
        {
            if( isNotMonitoring(it.first) )
            {
                debugMessage("ca_create_subscription to ", ENUM_TO_STRING(it.first));
                continuousMonitorStructs.push_back( new llrfStructs::monitorStruct() );
                continuousMonitorStructs.back() -> monType    = it.first;
                continuousMonitorStructs.back() -> llrfObj = &llrf;
                continuousMonitorStructs.back() -> interface  = this;
                ca_create_subscription(it.second.CHTYPE,
                                   it.second.COUNT,
                                   it.second.CHID,
                                   it.second.MASK,
                                   liberallrfInterface::staticEntryLLRFMonitor,
                                   (void*)continuousMonitorStructs.back(),
                                   &continuousMonitorStructs.back() -> EVID);
            }
        }
    }
    int status = sendToEpics( "ca_create_subscription", "Succesfully Subscribed to LLRF Trace Monitors", "!!TIMEOUT!! Subscription to LLRF Trace monitors failed" );
    if ( status == ECA_NORMAL )
        debugMessage("All LLRF Trace Monitors Started");

}
//____________________________________________________________________________________________
bool liberallrfInterface::startTraceMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    if( Is_TracePV(pv) )
    {
        if( isNotMonitoring(pv) )
        {
            debugMessage("ca_create_subscription to ", ENUM_TO_STRING(pv));
            continuousMonitorStructs.push_back( new llrfStructs::monitorStruct() );
            continuousMonitorStructs.back() -> monType   = pv;
            continuousMonitorStructs.back() -> llrfObj   = &llrf;
            continuousMonitorStructs.back() -> interface = this;
            ca_create_subscription(llrf.pvMonStructs.at(pv).CHTYPE,
                               llrf.pvMonStructs.at(pv).COUNT,
                               llrf.pvMonStructs.at(pv).CHID,
                               llrf.pvMonStructs.at(pv).MASK,
                               liberallrfInterface::staticEntryLLRFMonitor,
                               (void*)continuousMonitorStructs.back(),
                               &continuousMonitorStructs.back() -> EVID);
        }
    }
    int status = sendToEpics( "ca_create_subscription", "Succesfully Subscribed to LLRF Trace Monitors", "!!TIMEOUT!! Subscription to LLRF Trace monitors failed" );
    if ( status == ECA_NORMAL )
        debugMessage(ENUM_TO_STRING(pv)," Monitor Started");
    return isMonitoring(pv);
}
//____________________________________________________________________________________________
bool liberallrfInterface::startCavFwdTraceMonitor()
{
    return startTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_CAV_FWD);
}
//____________________________________________________________________________________________
bool liberallrfInterface::startCavRevTraceMonitor()
{
    return startTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_CAV_REV);
}
//____________________________________________________________________________________________
bool liberallrfInterface::startKlyFwdTraceMonitor()
{
    return startTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_KLY_FWD);
}
//____________________________________________________________________________________________
bool liberallrfInterface::startKlyRevTraceMonitor()
{
    return startTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_KLY_REV);
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopTraceMonitoring(llrfStructs::LLRF_PV_TYPE pv)
{
    for( auto && it : continuousMonitorStructs )
    {
        if( Is_TracePV( it->monType)  )
        {
            if( it->monType == pv )
            {
                killMonitor( it );
                delete it;
            }
        }
    }
    return isNotMonitoring(pv);
}
//____________________________________________________________________________________________
void liberallrfInterface::stopTraceMonitoring()
{
    for( auto && it : continuousMonitorStructs )
    {
        if( Is_TracePV( it->monType)  )
        {
            killMonitor( it );
            delete it;
        }
    }
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopCavFwdTraceMonitor()
{
    return stopTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_CAV_FWD);
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopCavRevTraceMonitor()
{
    return stopTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_CAV_REV);
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopKlyFwdTraceMonitor()
{
    return stopTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_KLY_FWD);
}
//____________________________________________________________________________________________
bool liberallrfInterface::stopKlyRevTraceMonitor()
{
    return stopTraceMonitoring(llrfStructs::LLRF_PV_TYPE::LIB_KLY_REV);
}
//____________________________________________________________________________________________
void liberallrfInterface::staticEntryLLRFMonitor(const event_handler_args args)
{
    llrfStructs::monitorStruct*ms = static_cast<  llrfStructs::monitorStruct *>(args.usr);
    switch(ms -> monType)
    {
        case llrfStructs::LLRF_PV_TYPE::LIB_LOCK:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_LOCK = ",*(bool*)args.dbr);
            ms->interface->llrf.islocked = *(bool*)args.dbr;
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_AMP_FF:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_AMP_FF = ",*(double*)args.dbr);
            ms->llrfObj->amp_ff = *(double*)args.dbr;
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_AMP_SP:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_AMP_SP = ",*(double*)args.dbr);
            ms->llrfObj->amp_sp = *(double*)args.dbr;
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_PHI_FF:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PHI_FF = ",*(double*)args.dbr);
            ms->llrfObj->phi_ff = *(double*)args.dbr;
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_PHI_SP:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PHI_SP = ",*(double*)args.dbr);
            ms->llrfObj->phi_sp = *(double*)args.dbr;
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_CAV_FWD:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_CAV_FWD = ",*(double*)args.dbr);
            ms->interface->updateTrace(args,ms->llrfObj->cav_f_power);
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_CAV_REV:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_CAV_REV = ",*(double*)args.dbr);
            ms->interface->updateTrace(args, ms->llrfObj->cav_r_power);
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_KLY_FWD:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_KLY_FWD = ",*(double*)args.dbr);
            ms->interface->updateTrace(args,ms->llrfObj->kly_f_power);
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_KLY_REV:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_KLY_REV = ",*(double*)args.dbr);
            ms->interface->updateTrace(args,ms->llrfObj->kly_r_power);
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_TIME_VECTOR:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_TIME_VECTOR = ",*(double*)args.dbr);
            ms->interface->updateTrace(args,ms->llrfObj->time_vector);
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_PULSE_LENGTH:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PULSE_LENGTH = ",*(double*)args.dbr);
            ms->llrfObj->pulse_length = *(double*)args.dbr;
            break;
        case llrfStructs::LLRF_PV_TYPE::LIB_PULSE_OFFSET:
            ms->interface->debugMessage("staticEntryLLRFMonitor LIB_PULSE_OFFSET = ",*(double*)args.dbr);
            ms->llrfObj->pulse_offset = *(double*)args.dbr;
            break;
        default:
            ms->interface->debugMessage("ERROR staticEntryLLRFMonitor passed UNknown PV Type");
            // ERROR
    }
}
//____________________________________________________________________________________________
void liberallrfInterface::updateTrace(const event_handler_args args,llrfStructs::rf_trace_data& trace)
{
    trace.count += 1;
    message( "count = ", trace.count  );

//    const dbr_time_double * p = ( const struct dbr_time_double * ) args.dbr;
//    beamPositionMonitorStructs::rawDataStruct * bpmdo = reinterpret_cast< beamPositionMonitorStructs::rawDataStruct *> (ms -> val);

    //const dbr_time_double * p = ( const struct dbr_time_double * ) args.dbr;

    size_t i = 0;
    for( auto && it : trace.value)
    {
        it = *( (double*)args.dbr + i);
//        std::cout << trace.second[i] << " ";
        ++i;
    }

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
bool liberallrfInterface::isLocked()
{
    return llrf.islocked;
}
//____________________________________________________________________________________________
llrfStructs::LLRF_TYPE liberallrfInterface::getType()
{
    return llrf.type;
}
//____________________________________________________________________________________________
size_t liberallrfInterface::getPowerTraceLength()
{
    return llrf.powerTraceLength;
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavRevPower()
{
    return llrf.cav_r_power.value;
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getCavFwdPower()
{
    return llrf.cav_f_power.value;
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyRevPower()
{
    return llrf.kly_f_power.value;
}
//____________________________________________________________________________________________
std::vector<double> liberallrfInterface::getKlyFwdPower()
{
    return llrf.kly_f_power.value;
}
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
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_AMP_SP),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAmpFF(double value)
{
    return setValue(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::LIB_AMP_FF),value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAmpLLRF(double value)
{
    return setAmpFF(value);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setPhiLLRF(double value)
{
    return setPhiFF(value);
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

        if( val > llrf.maxAmp )
        {
            message("Error!! Requested amplitude, ",val,"  too high");
        }
        else
        {
            debugMessage("Requested amplitude, ", value," MV / m = ", val," in LLRF units ");
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
bool liberallrfInterface::setPHIDEG()
{// ONLY ever called from staticEntryLLRFMonitor
    double val = (llrf.phi_ff) * (llrf.phiCalibration);
    debugMessage("setPHIDEG PHI_DEG to, ",val, ", calibration = ", llrf.phiCalibration);
    return setValue2(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::PHI_DEG),val);
}
//____________________________________________________________________________________________
bool liberallrfInterface::setAMPMVM()
{// ONLY ever called from staticEntryLLRFMonitor
    double val = (llrf.amp_ff) * (llrf.ampCalibration);
    debugMessage("setAMPMVM AMP_MVM to, ",val, ", calibration = ", llrf.ampCalibration);
    return setValue2(llrf.pvMonStructs.at(llrfStructs::LLRF_PV_TYPE::AMP_MVM),val);
}
//____________________________________________________________________________________________
const llrfStructs::liberallrfObject& liberallrfInterface::getLLRFObjConstRef()
{
    return llrf;
}
//____________________________________________________________________________________________
template<typename T>
bool liberallrfInterface::setValue( llrfStructs::pvStruct& pvs, T value)
{
    bool ret = false;
    ca_put(pvs.CHTYPE,pvs.CHID,&value);
    std::stringstream ss;
    ss << "setValue setting " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    message(ss);
    ss.str("");
    ss << "Timeout setting llrf, " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    int status = sendToEpics("ca_put","",ss.str().c_str());
    if(status==ECA_NORMAL)
        ret=true;
    return ret;
}
//____________________________________________________________________________________________
template<typename T>
bool liberallrfInterface::setValue2( llrfStructs::pvStruct& pvs, T value)
{
    bool ret = false;
    ca_put(pvs.CHTYPE,pvs.CHID,&value);
    std::stringstream ss;
    ss << "setValue2 setting " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    message(ss);
    ss.str("");
    ss << "Timeout setting llrf, " << ENUM_TO_STRING(pvs.pvType) << " value to " << value;
    int status = sendToEpics2("ca_put","",ss.str().c_str());
    if(status==ECA_NORMAL)
        ret=true;
    return ret;
}
//____________________________________________________________________________________________
bool liberallrfInterface::Is_TracePV(llrfStructs::LLRF_PV_TYPE pv)
{
    bool r = false;
    if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CAV_FWD)
    {
        r = true;
    }
    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_CAV_REV)
    {
        r = true;
    }
    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_KLY_FWD)
    {
        r = true;
    }
    else if(pv == llrfStructs::LLRF_PV_TYPE::LIB_KLY_REV)
    {
        r = true;
    }
    return r;
}
//____________________________________________________________________________________________
bool liberallrfInterface::IsNot_TracePV(llrfStructs::LLRF_PV_TYPE pv)
{
    return !Is_TracePV(pv);
}








