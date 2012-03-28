CONFIG += ordered
TEMPLATE = subdirs

EXTRASUBDIRS = \
        downloadspeed \
        environmentvariable \
        extractarchiveoperationtest \
        fileengineclient \
        fileengineserver


include(../installerfw.pri)

!isEqual(IFW_SOURCE_TREE, $$IFW_BUILD_TREE) {
    for(SUBDIR, EXTRASUBDIRS) {
        mkdir.commands += $$QMAKE_MKDIR $$SUBDIR $${IFW_NEWLINE}
    }
    QMAKE_EXTRA_TARGETS += mkdir
}

for(SUBDIR, EXTRASUBDIRS) {
    tests.commands += cd $$SUBDIR && $(QMAKE) -r $$PWD/$$SUBDIR && $(MAKE) $${IFW_NEWLINE}
}
!isEqual(IFW_SOURCE_TREE, $$IFW_BUILD_TREE) {
    tests.depends = mkdir
}
QMAKE_EXTRA_TARGETS += tests