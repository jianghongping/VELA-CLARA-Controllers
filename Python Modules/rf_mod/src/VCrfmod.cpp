#include "VCrfmod.h"
#include <iostream>

VCrfmod::VCrfmod():
VCbase("VCrfmod"),
virtual_GUN_MOD_Controller_Obj(nullptr),
physical_GUN_MOD_Controller_Obj(nullptr),
virtual_L01_MOD_Controller_Obj(nullptr),
physical_L01_MOD_Controller_Obj(nullptr),
offline_L01_MOD_Controller_Obj(nullptr),
gunModConf(UTL::APCLARA1_CONFIG_PATH + UTL::RF_GUN_MOD_CONFIG),
l01ModConf(UTL::APCLARA1_CONFIG_PATH + UTL::L01_MOD_CONFIG),
withEPICS(true),
withoutEPICS(false),
withoutVM(false),
withVM(true)
{
   std::cout << "Instantiated a VCrfmod " << std::endl;

    //ctor
}
//______________________________________________________________________________
VCrfmod::~VCrfmod()
{
    //dtor
    if(virtual_GUN_MOD_Controller_Obj)
    {
        delete virtual_GUN_MOD_Controller_Obj;
               virtual_GUN_MOD_Controller_Obj = nullptr;
    }
    if(physical_GUN_MOD_Controller_Obj)
    {
        delete physical_GUN_MOD_Controller_Obj;
               physical_GUN_MOD_Controller_Obj = nullptr;
    }
    if(offline_GUN_MOD_Controller_Obj)
    {
        delete offline_GUN_MOD_Controller_Obj;
               offline_GUN_MOD_Controller_Obj = nullptr;
    }
    if(virtual_L01_MOD_Controller_Obj)
    {
        delete virtual_L01_MOD_Controller_Obj;
               virtual_L01_MOD_Controller_Obj = nullptr;
    }
    if(physical_L01_MOD_Controller_Obj)
    {
        delete physical_L01_MOD_Controller_Obj;
               physical_L01_MOD_Controller_Obj = nullptr;
    }
    if(offline_L01_MOD_Controller_Obj)
    {
        delete offline_L01_MOD_Controller_Obj;
               offline_L01_MOD_Controller_Obj = nullptr;
    }
}
//______________________________________________________________________________
gunModController& VCrfmod::virtual_GUN_MOD_Controller()
{
    const std::string name  = "virtual_GUN_MOD_Controller";
    return getGunController(virtual_GUN_MOD_Controller_Obj,
                                           l01ModConf,
                                           name,withVM,withEPICS,
                                           HWC_ENUM::MACHINE_AREA::RF_L01);
}
//______________________________________________________________________________
gunModController& VCrfmod::physical_GUN_MOD_Controller()
{
    const std::string name  = "physical_GUN_MOD_Controller";
    return getGunController(physical_GUN_MOD_Controller_Obj,
                                           l01ModConf,
                                           name,withoutVM,withEPICS,
                                           HWC_ENUM::MACHINE_AREA::RF_L01);
}
//______________________________________________________________________________
gunModController& VCrfmod::offline_GUN_MOD_Controller()
{
    const std::string name  = "offline_GUN_MOD_Controller";
    return getGunController(offline_GUN_MOD_Controller_Obj,
                                           l01ModConf,
                                           name,withoutVM,withoutEPICS,
                                           HWC_ENUM::MACHINE_AREA::RF_L01);
}
//______________________________________________________________________________
l01ModController& VCrfmod::virtual_L01_MOD_Controller()
{
    const std::string name  = "virtual_L01_MOD_Controller";
    return getL01Controller(virtual_L01_MOD_Controller_Obj,
                                           l01ModConf,
                                           name,withVM,withEPICS,
                                           HWC_ENUM::MACHINE_AREA::RF_L01);
}
//______________________________________________________________________________
l01ModController& VCrfmod::physical_L01_MOD_Controller()
{
    const std::string name  = "physical_L01_MOD_Controller";
    return getL01Controller(physical_L01_MOD_Controller_Obj,
                                           l01ModConf,
                                           name,withoutVM,withEPICS,
                                           HWC_ENUM::MACHINE_AREA::RF_L01);
}
//______________________________________________________________________________
l01ModController& VCrfmod::offline_L01_MOD_Controller()
{
    const std::string name  = "offline_L01_MOD_Controller";
    return getL01Controller(offline_L01_MOD_Controller_Obj,
                                           l01ModConf,
                                           name,withoutVM,withoutEPICS,
                                           HWC_ENUM::MACHINE_AREA::RF_L01);
}
//______________________________________________________________________________
l01ModController& VCrfmod::getL01Controller(l01ModController*& cont,
                           const std::string& conf,
                           const std::string& name,
                           const bool shouldVM,
                           const bool shouldEPICS,
                           const HWC_ENUM::MACHINE_AREA myMachineArea)
{
    if(cont)
    {
        std::cout << name << " object already exists," <<std::endl;
    }
    else
    {
        std::cout << "Creating " << name << " object" <<std::endl;

        messageStates[cont].first  = shouldShowMessage;
        messageStates.at(cont).second = shouldShowDebugMessage;

        std::cout << "shouldShowMessage = " << shouldShowMessage << std::endl;
        std::cout << "shouldShowDebugMessage = " << shouldShowDebugMessage << std::endl;


        cont = new l01ModController(messageStates.at(cont).first,messageStates.at(cont).second,l01ModConf,shouldVM,shouldEPICS);

    }
    return *cont;
}
//______________________________________________________________________________
gunModController& VCrfmod::getGunController(gunModController*& cont,
                                           const std::string& conf,
                                           const std::string& name,
                                           const bool shouldVM,
                                           const bool shouldEPICS,
                                           const HWC_ENUM::MACHINE_AREA myMachineArea)
{
    if(cont)
    {
        std::cout << name << " object already exists," <<std::endl;
    }
    else
    {
        std::cout << "Creating " << name << " object" <<std::endl;
        messageStates[cont].first  = shouldShowMessage;
        messageStates.at(cont).second = shouldShowDebugMessage;

        std::cout << "shouldShowMessage = " << shouldShowMessage << std::endl;
        std::cout << "shouldShowDebugMessage = " << shouldShowDebugMessage << std::endl;

        cont = new gunModController(messageStates.at(cont).first,messageStates.at(cont).second, gunModConf, shouldVM, shouldEPICS);
    }
    return *cont;
}
//______________________________________________________________________________
void VCrfmod::updateMessageStates()
{
    for(auto&& it:messageStates)
    {
        it.second.first  = shouldShowMessage;
        it.second.second = shouldShowDebugMessage;
    }
}


