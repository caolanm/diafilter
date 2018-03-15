/*************************************************************************
 *
 * Caolán McNamara
 * Copyright 2010 by Red Hat, Inc. 
 *
 * openoffice.org-diafilter is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 3 or
 * later, as published by the Free Software Foundation.
 *
 * openoffice.org-diafilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 *
 * You should have received a copy of the GNU General Public License version 3
 * along with openoffice.org-diafilter.  If not, see
 * <http://www.gnu.org/copyleft/gpl.html> for a copy of the GPLv3 License.
 *
 ************************************************************************/

#include <cppuhelper/factory.hxx>
#include <cppuhelper/implementationentry.hxx>
#include <uno/lbnames.h>
#include <com/sun/star/uno/Reference.hxx>

#include "filters.hxx"

using namespace ::com::sun::star::registry;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;

namespace
{
	static ::cppu::ImplementationEntry const regEntries[] = {
		{ &DIAFilter::get,
		  &DIAFilter::getImplementationName_static,
		  &DIAFilter::getSupportedServiceNames_static,
		  &::cppu::createSingleComponentFactory, 0, 0 },
		{ &DIAShapeFilter::get,
		  &DIAShapeFilter::getImplementationName_static,
		  &DIAShapeFilter::getSupportedServiceNames_static,
		  &::cppu::createSingleComponentFactory, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 }
	};
}

extern "C" SAL_DLLPUBLIC_EXPORT void SAL_CALL component_getImplementationEnvironment(sal_Char const ** ppEnvTypeName, uno_Environment **)
{
	*ppEnvTypeName = CPPU_CURRENT_LANGUAGE_BINDING_NAME ":unsafe";
} 

extern "C" SAL_DLLPUBLIC_EXPORT sal_Bool SAL_CALL component_writeInfo(void * serviceManager, void * registryKey)
{
	return ::cppu::component_writeInfoHelper(serviceManager, registryKey, regEntries);
}

extern "C" SAL_DLLPUBLIC_EXPORT void * SAL_CALL component_getFactory(const char * implName, void * serviceManager, void * registryKey)
{
	return ::cppu::component_getFactoryHelper(implName, serviceManager, registryKey, regEntries);
}
