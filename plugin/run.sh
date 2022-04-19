#I want to do the following in this file:
#1. calculate the checksum of the file and return it somewhere
#2. compare the checksum to the list of checksums in the db
    #sha256:<pastedchecksum>
#3. if the checksums match, print an alert with the malware name and family
#After that, I need to figure out how to implement bash into the plugin.

check = sha256 -c plugin/checksums
echo 'created checksum'

sudo apt install wget
echo 'wget installed'

search = wget --post-data "query=get_info&hash=$check" https://mb-api.abuse.ch/api/v1/

if [ $search ==  'signature_not_found' || $search == 'no_results' ]
then
    echo 'The file does not match any known malware'
else
    echo 'we found something! Here is some more interesting information'
    echo $search
fi
