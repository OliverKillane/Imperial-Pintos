from os import linesep
import sys
import subprocess

subprocess.run(["clang-format","-style=file","-i", sys.argv[1]])

#Reformats comments
with open(sys.argv[1], "r") as f:
    rawString = f.read()
    inComment = False
    output = ""

    inComment = False
    for line in rawString.splitlines():
        commentStartPos = line.find("/*")
        commentEndPos = line.find("*/")

        if (commentStartPos != -1):
            lastStartPos = commentStartPos

        # /* One line Comment */
        if (commentStartPos != -1 and commentEndPos != -1):
            output += line.rstrip() + "\n"

        # /* Beggining of block comment
        elif (commentStartPos != -1 and commentEndPos == -1):
            inComment = True
            output += line.rstrip() + "\n"
        
        # End of block comment */
        elif (commentStartPos == -1 and commentEndPos != -1):
            inComment = False
            if (line.lstrip()[0] != "*"):
                output += (("\t" * lastStartPos) + " * " + 
                          line.replace("*/","").lstrip().rstrip() + 
                          "\n" + ("\t" * lastStartPos) + " */\n")
            else:
                output += line.rstrip() + "\n"
        
        # In the middle of a block comment
        elif(inComment == True):
            if (not line.lstrip().startswith("*")):
                if len(line.lstrip()) != 0:
                    output += ("\t" * lastStartPos) + " * " +  line.lstrip() + "\n"
                else:
                    output += ("\t" * lastStartPos) + " *\n"
            else:
                output += line.rstrip() + "\n"
        
        #Normal line
        else:
            output += line.rstrip() + "\n"

with open(sys.argv[1], "w") as f:
    f.write(output)

