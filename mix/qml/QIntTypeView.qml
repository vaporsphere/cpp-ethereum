import QtQuick 2.0

Item
{
	property alias text: textinput.text
	id: editRoot

	SourceSansProBold
	{
		id: boldFont
	}

	Rectangle {
		anchors.fill: parent
		radius: 4
		color: "#f7f7f7"
		TextInput {
			id: textinput
			text: text
			anchors.fill: parent
			font.family: boldFont.name
			clip: true
			MouseArea {
				id: mouseArea
				anchors.fill: parent
				hoverEnabled: true
				onClicked: textinput.forceActiveFocus()
			}
		}
	}
}



