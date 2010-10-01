TEMPLATE=subdirs

QMAKE_EXTRA_TARGETS += metadata initScript
XMLLINT= xmllint --noout --schema /usr/share/test-definition/testdefinition-syntax.xsd --schema /usr/share/test-definition/testdefinition-tm_terms.xsd 

SUITENAME=mcompositor-functional-tests
scripts.path=/usr/share/meegotouch/testscripts/$$SUITENAME
scripts.files += \
    test*.py \


metadata.commands =  ./createTestXml $${scripts.path} $${scripts.files}; 
metadata.commands +=  $$XMLLINT tests.xml

metadata.target=tests.xml

metadata.path=/usr/share/$$SUITENAME

metadata.files += tests.xml 

metadata.CONFIG += no_check_exist

initScript.files = mcompositor-test-init.py
initScript.path = /usr/bin

INSTALLS += metadata scripts initScript
