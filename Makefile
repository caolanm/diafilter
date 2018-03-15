##########################################################################
# Copyright (C) 2010 Caolán McNamara <caolanm@redhat.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
##########################################################################


# Check that build environment is properly set up
ifndef OO_SDK_HOME
$(error You must run setsdkenv before running make)
endif
ifndef DIA_SHAPES_DIR
$(error You must set DIA_SHAPES_DIR, e.g. DIA_SHAPES_DIR=/usr/share/dia/shapes make)
endif
DIA_GALLERY_DIR=gallery
PRJ=$(OO_SDK_HOME)
# Load SDK settings
include $(PRJ)/settings/settings.mk
include $(PRJ)/settings/std.mk

DIAFILTER_PACKAGENAME=diafilter
DIAFILTER_VERSION=1.7.5

# Platform specific variables
ifeq "$(PLATFORM)" "windows"
        WARNING_FLAGS=-Wall -WX -wd4061 -wd4365 -wd4514 -wd4625 -wd4626 -wd4668 -wd4711 -wd4820
        # The following warnings should be fixed in the future
        WARNING_FLAGS+= -wd4640
else
        WARNING_FLAGS=-Wall -Wno-non-virtual-dtor
endif
ifeq "$(PLATFORM)" "linux"
        LINKER_FLAGS=-Wl,--no-undefined
endif

LINK_FLAGS=$(COMP_LINK_FLAGS) $(OPT_FLAGS) $(LINKER_FLAGS) $(LINK_LIBS) \
           $(CPPUHELPERLIB) $(CPPULIB) $(SALLIB) $(STLPORTLIB) -lz

PLATFORMSTRING:=$(shell echo $(UNOPKG_PLATFORM) | tr A-Z a-z)
DIAFILTER_EXTENSION_SHAREDLIB=diafilter.$(UNOPKG_PLATFORM).$(SHAREDLIB_EXT)
DIAFILTER_OBJECTS=services diafilter shapefilter \
	saxattrlist \
	gz_inputstream \
	comphelper/string \
	i18npool/paper \
	basegfx/b2dpolygon \
	basegfx/b2dsvgpolypolygon \
	basegfx/b2dpolygontools \
	basegfx/b2dvector \
	basegfx/b2dtuple \
	basegfx/ftools \
	basegfx/b2dhommatrix \
	basegfx/b2dpoint \
	basegfx/b2dcubicbezier \
	basegfx/b2dpolypolygon \
	basegfx/b2dhommatrixtools \
	basegfx/b3dpolygon \
	basegfx/b3dhommatrix \
	basegfx/b3dpoint \
	basegfx/b2dbeziertools \
	basegfx/b3dtuple \
	basegfx/b3dvector

DIAFILTER_HEADERS=
COPY_TEMPLATES=dia_filters.xcu dia_types.xcu Paths.xcu help/component.txt
COPY_SHAPES:=$(shell find $(DIA_SHAPES_DIR) -name "*.shape" -print)
COPY_GALLERY:=$(shell find $(DIA_GALLERY_DIR) -name "sg*.???" -print)
SED=sed

comma:=,

EXTENSION_FILES=build/oxt/META-INF/manifest.xml build/oxt/description.xml \
	      build/oxt/$(DIAFILTER_EXTENSION_SHAREDLIB) \
	      $(patsubst %,build/oxt/%,$(COPY_TEMPLATES)) \
              $(patsubst $(DIA_SHAPES_DIR)/%,build/oxt/shapes/%,$(COPY_SHAPES)) \
              $(patsubst $(DIA_GALLERY_DIR)/%,build/oxt/gallery/%,$(COPY_GALLERY))

# Targets
.PHONY: all clean

oxt: $(EXTENSION_FILES)
	@cd build/oxt && $(SDK_ZIP) -q -r -9 ../$(DIAFILTER_PACKAGENAME).oxt \
	   $(patsubst build/oxt/%,%,$^)
	@echo created build/$(DIAFILTER_PACKAGENAME).oxt
	@echo To install for current user use e.g.: unopkg add build/diafilter.oxt

all: oxt

# Sed scripts for modifying templates
MANIFEST_SEDSCRIPT:=s/DIAFILTER_EXTENSION_SHAREDLIB/$(DIAFILTER_EXTENSION_SHAREDLIB)/g;s/UNOPKG_PLATFORM/$(UNOPKG_PLATFORM)/g
DESCRIPTION_SEDSCRIPT:=s/DIAFILTER_VERSION/$(DIAFILTER_VERSION)/g;s/PLATFORMSTRING/$(PLATFORMSTRING)/g
MANIFEST_SEDSCRIPT:="$(MANIFEST_SEDSCRIPT)"
DESCRIPTION_SEDSCRIPT:="$(DESCRIPTION_SEDSCRIPT)"

# Create extension files
build/oxt/META-INF/manifest.xml: oxt/META-INF/manifest.xml.template
	@$(MKDIR) $(subst /,$(PS),$(@D))
	@$(SED) -e $(MANIFEST_SEDSCRIPT) < $^ > $@

build/oxt/description.xml: oxt/description.xml.template
	@-$(MKDIR) $(subst /,$(PS),$(@D))
	@$(SED) -e $(DESCRIPTION_SEDSCRIPT) < $^ > $@

$(patsubst %,build/oxt/%,$(COPY_TEMPLATES)): build/oxt/%: oxt/%
	@-$(MKDIR) $(subst /,$(PS),$(@D))
	@$(COPY) "$(subst /,$(PS),$^)" "$(subst /,$(PS),$@)"

$(patsubst $(DIA_SHAPES_DIR)/%,build/oxt/shapes/%,$(COPY_SHAPES)): build/oxt/shapes/%: $(DIA_SHAPES_DIR)/%
	@$(MKDIR) $(subst /,$(PS),$(@D))
	@$(COPY) "$(subst /,$(PS),$^)" "$(subst /,$(PS),$@)"

$(patsubst $(DIA_GALLERY_DIR)/%,build/oxt/gallery/%,$(COPY_GALLERY)): build/oxt/gallery/%: $(DIA_GALLERY_DIR)/%
	@$(MKDIR) $(subst /,$(PS),$(@D))
	@$(COPY) "$(subst /,$(PS),$^)" "$(subst /,$(PS),$@)"

$(patsubst %,build/oxt/%,$(STANDALONE_EXTENSION_FILES)): build/oxt/%: $(STANDALONE_EXTENSION_PATH)/%
	@$(MKDIR) $(subst /,$(PS),$(@D))
	@$(COPY) -r "$(subst /,$(PS),$^)" "$(subst /,$(PS),$@)"

# Type library C++ headers
build/hpp.flag:
	-$(MKDIR) build/hpp
	$(CPPUMAKER) -Gc -O./build/hpp $(URE_TYPES) $(OFFICE_TYPES)
	echo flagged > $@


# Compile the C++ source files
build/src/%.$(OBJ_EXT): src/%.cxx build/hpp.flag
	-$(MKDIR) $(subst /,$(PS),$(@D))
	$(CC) -Isrc -Isrc/basegfx $(CC_FLAGS) $(OPT_FLAGS) $(WARNING_FLAGS) -Ibuild/hpp -I$(PRJ)/include/stl -I$(PRJ)/include $(CC_DEFINES) $(CC_OUTPUT_SWITCH)$@ $<

# Link the shared library
build/oxt/$(DIAFILTER_EXTENSION_SHAREDLIB): $(patsubst %,build/src/%.$(OBJ_EXT),$(DIAFILTER_OBJECTS))
	$(LINK) $(subst '-Wl$(comma)-rpath$(comma)$$ORIGIN',,$(LINK_FLAGS)) -o $@ $^

dist:
	rm -rf openoffice.org-diafilter-$(DIAFILTER_VERSION)
	mkdir openoffice.org-diafilter-$(DIAFILTER_VERSION)
	cp -pR Makefile *.desktop src *.txt NEWS oxt tests gallery README TODO openoffice.org-diafilter-$(DIAFILTER_VERSION)
	tar --exclude-vcs --exclude-backups --exclude=build --exclude="*.bz2" -cjf openoffice.org-diafilter-$(DIAFILTER_VERSION).tar.bz2 openoffice.org-diafilter-$(DIAFILTER_VERSION)
	rm -rf openoffice.org-diafilter-$(DIAFILTER_VERSION)

# The clean target
clean:
	rm -rf build
