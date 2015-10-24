# osg and osgearth emit a lot of unused parameter warnings...
QMAKE_CXXFLAGS += -Wno-unused-parameter

OSG_SDK_DIR = $$clean_path($$(OSG_SDK_DIR))
message(Using osg from here: $$OSG_SDK_DIR)

INCLUDEPATH += $$OSG_SDK_DIR/include

linux {
    exists( $$OSG_SDK_DIR/lib64 ) {
        LIBS += -L$$OSG_SDK_DIR/lib64
    } else {
        LIBS += -L$$OSG_SDK_DIR/lib
    }

    LIBS +=-lOpenThreads
    LIBS += -losg -losgUtil -losgDB -losgGA -losgFX -losgViewer -losgText -losgQt
    LIBS += -losgEarth -losgEarthUtil -losgEarthFeatures -losgEarthSymbology -losgEarthAnnotation -losgEarthQt
}

macx {
    LIBS += -L$$OSG_SDK_DIR/lib

    LIBS += -lOpenThreads
    LIBS += -losg -losgUtil -losgDB -losgGA -losgFX -losgViewer -losgText -losgQt
    LIBS += -losgEarth -losgEarthUtil -losgEarthFeatures -losgEarthSymbology -losgEarthAnnotation -losgEarthQt
    LIBS += -losgDB
}

win32 {
    LIBS += -L$$OSG_SDK_DIR/lib

    #CONFIG(release, debug|release) {
        LIBS += -lOpenThreads
        LIBS += -losg -losgUtil -losgDB -losgGA -losgFX -losgViewer -losgText -losgQt
        LIBS += -losgEarth -losgEarthUtil -losgEarthFeatures -losgEarthSymbology -losgEarthAnnotation -losgEarthQt
    #}
    #CONFIG(debug, debug|release) {
    #    LIBS += -lOpenThreadsd
    #    LIBS += -losgd -losgUtild -losgDBd -losgGAd -losgFXd -losgViewerd -losgTextd -losgQtd
    #    LIBS += -losgEarthd -losgEarthUtild -losgEarthFeaturesd -losgEarthSymbologyd -losgEarthAnnotationd -losgEarthQtd
    #}
}
