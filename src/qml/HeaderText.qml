import QtQuick 6.0

Text{
    FontLoader{
        id: localFont
        source: "qrc:/SourceSansPro-Regular.ttf"
    }
    font.family: localFont.name
    font.pointSize: 40
    color: "white"
}
