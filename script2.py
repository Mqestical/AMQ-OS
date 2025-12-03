import os

# List of directories to count lines in
directories = ["src", "includes"]

# Optional: only count certain file types
file_extensions = [".c", ".h", ".cpp", ".py"]

def count_lines_in_file(file_path):
    with open(file_path, 'r', errors='ignore') as f:
        return sum(1 for _ in f)

def count_lines_in_dir(dir_path):
    total_lines = 0
    for root, dirs, files in os.walk(dir_path):
        for file in files:
            if any(file.endswith(ext) for ext in file_extensions):
                file_path = os.path.join(root, file)
                lines = count_lines_in_file(file_path)
                total_lines += lines
    return total_lines

if __name__ == "__main__":
    grand_total = 0
    for directory in directories:
        if os.path.exists(directory):
            lines = count_lines_in_dir(directory)
            print(f"Lines in {directory}: {lines}")
            grand_total += lines
    print(f"Total lines of code: {grand_total}")
