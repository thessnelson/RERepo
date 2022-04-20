from asyncio.windows_events import NULL
import subprocess
import os
import glob
import hashlib
from malwarebazaar.api import Bazaar

#start by getting the current directory
path = os.getcwd()

files = glob.iglob(path, recursive = True)

#bash command: sha256sum filename.ext
for file in files:

    check = hashlib.sha256(files).hexdigest()
    link = 'https://bazaar.abuse.ch/sample/' + check + '/'

    #open up abuse.ch
    bazaar = Bazaar("myapikey")
    status = bazaar.query_hash(check)

    #write the results.
    with open("checksum.txt", "wb") as f:
        f.write(files + ": " + check + "\n")
        f.write("file status" + status + "\n")

        if status != NULL or status != "":
            f.write("link: " + link)

        f.close()
 
#now figure out how to add the link to the analysis to the top of the file in ghidra!