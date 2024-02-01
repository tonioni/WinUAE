def extract_srcs_from_file(file):
    with open(file, 'r') as f:
        text = f.read()

        lines = text.split('\n')
        files = []
        srcs_found = False

        for line in lines:
            if line.startswith('srcs ='):
                srcs_found = True
                line = line.replace('srcs =', '')

            if srcs_found:
                # Continue appending files if the line ends with '\'
                if line.endswith('\\'):
                    files.extend(line[:-1].strip().split())
                else:
                    # Last line of files
                    files.extend(line.strip().split())
                    break
    return files

# read file to string
def read_file_to_string(file):
    with open(file, 'r') as f:
        text = f.read()
    return text

def main():
    header = read_file_to_string("build/vc_proj_header.txt")
    footer = read_file_to_string("build/vc_proj_footer.txt")
    files = extract_srcs_from_file("Makefile.winuae")

    with open("build/quaesar.vcxproj", 'w') as f:
        f.write(header)
        for file in files:
            f.write('<ClCompile Include="../' + file + '" />\n')
        f.write(footer)

if __name__ == "__main__":
    main()


