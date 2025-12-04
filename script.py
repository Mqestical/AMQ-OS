import os
import re

# Regex patterns
line_comment = re.compile(r"//.*?$", re.MULTILINE)
block_comment = re.compile(r"/\*.*?\*/", re.DOTALL)

def remove_comments_from_file(path):
    with open(path, "r", encoding="utf-8") as f:
        code = f.read()

    # Remove block comments first, then line comments
    code = re.sub(block_comment, "", code)
    code = re.sub(line_comment, "", code)

    # Remove trailing whitespace from cleaned lines
    code = "\n".join(line.rstrip() for line in code.splitlines())

    with open(path, "w", encoding="utf-8") as f:
        f.write(code)

def remove_comments_in_tree(root):
    for base, _, files in os.walk(root):
        for file in files:
            if file.endswith(".c") or file.endswith(".h"):
                full_path = os.path.join(base, file)
                print("Cleaning:", full_path)
                remove_comments_from_file(full_path)

if __name__ == "__main__":
    # Change this to your project root if needed
    project_root = "src"

    print("Removing comments in all C files under:", project_root)
    remove_comments_in_tree(project_root)
    print("Done.")
