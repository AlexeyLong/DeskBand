// factory.cpp - object creation helper
#include "pch.h"
#include "DeskBand.h"

CDeskBand* CreateDeskBand()
{
    return new (std::nothrow) CDeskBand();
}
