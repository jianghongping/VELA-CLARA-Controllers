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

#include "screenController.h"
//stl
#include <iostream>
#include <time.h>
//djs
#include "screenInterface.h"

///Initialsing
///Moving
///Checks
///Get Stuff


screenController::screenController( const std::string configFileLocation1,
                                    const std::string configFileLocation2, const bool show_messages, const bool show_debug_messages )
:controller( show_messages, show_debug_messages ),
localInterface( configFileLocation1, configFileLocation2, &SHOW_MESSAGES, &SHOW_DEBUG_MESSAGES )
{
   initialise();
}
//__________________________________________________________________________
screenController::screenController( const bool show_messages, const bool show_debug_messages )
: controller( show_messages, show_debug_messages ), localInterface( &SHOW_MESSAGES, &SHOW_DEBUG_MESSAGES )
{


   initialise();
}
//_____________________________________________________________________________
screenController::~screenController(){}
//dtor
//_______________________________________________________________________________
void screenController::initialise()
{
  localInterface.getScreenNames( ScreenNames );

  if( localInterface.interfaceInitReport() )
        message("screenController instantation success.");
}
//________________________________________________________________________________
void screenController::Screen_Out( const std::string & name )
{
    localInterface.Screen_Out( name );
}
//_________________________________________________________________________________
void screenController::Screen_In( const std::string & name )
{
    localInterface.Screen_In( name );
}
//__________________________________________________________________________________
void screenController::Screen_Move( const std::string & name, const std::string & position )
{
    localInterface.Screen_Move( name,position );
}
//_________________________________________________________________________________
void screenController::Screen_Stop( const std::string & name )
{
    localInterface.Stop( name );
}
//__________________________________________________________________________________
void screenController::All_Out()
{
    localInterface.All_Out();
}
//__________________________________________________________________________________
void screenController::controller_move_to_position( const std::string & name, const std::string & position )
{
    localInterface.move_to_position( name, position );
}
//___________________________________________________________________________________
void screenController::controller_move_to( const std::string & name, const std::string & V_H, const double & position )
{
    localInterface.move_to( name, V_H, position );
}
//___________________________________________________________________________________
bool screenController::IsOut( const std::string & name )
{
    return localInterface.IsOut( name );
}
//_______________________________________________________________________________________
bool screenController::IsIn( const std::string & name )
{
    return localInterface.IsIn( name );
}
//__________________________________________________________________________________________
bool screenController::horizontal_disabled_check( const std::string & name )
{
    return localInterface.horizontal_disabled_check( name );
}
//__________________________________________________________________________________________
bool screenController::vertical_disabled_check( const std::string &  name )
{
    return localInterface.vertical_disabled_check( name );
}
//____________________________________________________________________________________________
double screenController::get_CA_PEND_IO_TIMEOUT()
{
  return localInterface.get_CA_PEND_IO_TIMEOUT( );
}
//_____________________________________________________________________________________________
void screenController::set_CA_PEND_IO_TIMEOUT( double val )
{
    localInterface.set_CA_PEND_IO_TIMEOUT( val );
}
//________________________________________________________________________________________________________________
screenStructs::SCREEN_STATE screenController::getComplexHorizontalScreenState( const std::string & name )
{
    return localInterface.getComplexHorizontalScreenState( name );
}
//________________________________________________________________________________________________________________
screenStructs::SCREEN_STATE screenController::getComplexVerticalScreenState( const std::string & name )
{
    return localInterface.getComplexVerticalScreenState( name );
}
//______________________________________________________________________________________________________________
screenStructs::SCREEN_STATE screenController::getSimpleScreenState( const std::string & name )
{
    return localInterface.getSimpleScreenState( name );
}
//__________________________________________________________________________________________________
screenStructs::SCREEN_STATE screenController::getScreenState( const std::string & name )
{
    return localInterface.getScreenState( name );
}
//_________________________________________________________________________________________________
screenStructs::SCREEN_STATE screenController::getScreenState( const std::string & name, const std::string & V_H )
{
    return localInterface.getScreenState( name, V_H );
}
//___________________________________________________________________________________________________
double screenController::getComplexScreenHorizontalPosition( const std::string & name )
{
    return localInterface.getComplexScreenHorizontalPosition( name );
}
//____________________________________________________________________________________________________
double screenController::getComplexScreenVerticalPosition( const std::string & name )
{
    return localInterface.getComplexScreenVerticalPosition( name );
}
//____________________________________________________________________________________________________
double screenController::getScreenPosition( const std::string & name, const std::string & V_H )
{
    return localInterface.getScreenPosition(name, V_H );
}
//________________________________________________________________________________________________________________
std::string screenController::getComplexHorizontalScreenStateStr( const std::string & name )
{
    return ENUM_TO_STRING(localInterface.getComplexHorizontalScreenState( name ));
}
//________________________________________________________________________________________________________________
std::string screenController::getComplexVerticalScreenStateStr( const std::string & name )
{
    return ENUM_TO_STRING(localInterface.getComplexVerticalScreenState( name ));
}
//_______________________________________________________________________________________________________________
std::string screenController::getSimpleScreenStateStr( const std::string & name )
{
    return ENUM_TO_STRING(localInterface.getSimpleScreenState( name ));
}
//___________________________________________________________________________________________________________
std::string screenController::getScreenStateStr( const std::string & name )
{
    return ENUM_TO_STRING(localInterface.getScreenState( name ));
}
//_________________________________________________________________________________________________________
std::string screenController::getScreenStateStr( const std::string & name, const std::string & V_H )
{
    return ENUM_TO_STRING(localInterface.getScreenState( name, V_H ));
}
//______________________________________________________________________________________________
std::map< VELA_ENUM::ILOCK_NUMBER, VELA_ENUM::ILOCK_STATE > screenController::getILockStates( const std::string & name )
{
    return localInterface.getILockStates( name );
}
//____________________________________________________________________________________________________________________________
std::map< VELA_ENUM::ILOCK_NUMBER, std::string > screenController::getILockStatesStr( const std::string & name )
{
    return localInterface.getILockStatesStr( name );
}
//__________________________________________________________________________________________________________
void screenController::get_info( const std::string & name )
{
    return localInterface.get_info( name );
}
//__________________________________________________________________________________________________________
void screenController::get_config_values( const std::string & name )
{
    return localInterface.get_config_values( name );
}
//_________________________________________________________________________________________________________________
std::vector< std::string > screenController::getScreenNames()
{
    return ScreenNames;
}
