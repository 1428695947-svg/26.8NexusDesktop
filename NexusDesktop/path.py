import os
import json
import glob
import fnmatch

##############################################################
# 可配置参数 - 请根据您的项目需求修改以下参数
##############################################################

# 编译器路径
COMPILER_PATH = "D:/path/arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gcc.exe"

# C/C++标准配置
C_STANDARD = "c99"
CPP_STANDARD = "c++11"

# 宏定义
DEFINES = [
    "NDEBUG",
    "STM32F407xx",
    "USE_HAL_DRIVER"
]

# IntelliSense模式
INTELLISENSE_MODE = "windows-gcc-arm"

# 配置名称
CONFIG_NAME = "stm32f4"

# 1. 完全匹配排除（路径必须完全一致才排除）
EXACT_MATCH_EXCLUDE = [
    "${workspaceFolder}"
]

# 2. 相对路径格式排除（支持通配符 * 和 ?）
RELATIVE_PATH_EXCLUDE = [
    "third_lib/freertos/portable/GCC",
    "third_lib/freertos/portable/RVDS",
    "${workspaceFolder}/third_lib/lvgl/src"
]

# 3. 关键字排除（只要路径包含这些词就排除）
KEYWORD_EXCLUDE = [
]

# 是否在includePath中添加注释标记
ADD_COMMENT_MARKER = True
COMMENT_MARKER = "//以下为新增路径"

##############################################################
# 路径处理工具函数
##############################################################

def should_exclude(path, root_dir):
    """检查路径是否应该被排除，使用三种排除规则"""
    norm_path = path.replace("\\", "/").lower()
    root_dir_normalized = root_dir.replace("\\", "/").lower()

    # 1. 检查完全匹配排除（精确匹配）
    for exact_path in EXACT_MATCH_EXCLUDE:
        # 处理绝对路径格式
        abs_exact = exact_path.replace("${workspaceFolder}/", root_dir_normalized).replace("\\", "/").lower()
        if norm_path == abs_exact:
            return True

        # 处理${workspaceFolder}格式
        if norm_path == exact_path.replace("\\", "/").lower():
            return True

    # 2. 检查相对路径格式排除（支持通配符）
    for pattern in RELATIVE_PATH_EXCLUDE:
        # 将模式转换为相对路径格式
        rel_pattern = pattern.replace("${workspaceFolder}/", "").replace("\\", "/")

        # 将当前路径转换为相对路径
        try:
            if norm_path.startswith("${workspacefolder}/"):
                rel_path = norm_path[len("${workspacefolder}/"):]
            else:
                rel_path = norm_path.replace(root_dir_normalized, "").lstrip("/")
        except:
            rel_path = norm_path

        # 使用通配符匹配
        if fnmatch.fnmatch(rel_path, rel_pattern) or fnmatch.fnmatch(rel_path, rel_pattern + "/*"):
            return True

    # 3. 检查关键字排除
    for keyword in KEYWORD_EXCLUDE:
        if keyword.lower() in norm_path:
            return True

    return False

##############################################################
# 主功能函数
##############################################################

def find_include_dirs(root_dir):
    """查找所有包含头文件的目录（应用排除规则）"""
    include_dirs = set()

    # 首先处理根目录（如果未被排除）
    root_path = "${workspaceFolder}"
    if not should_exclude(root_path, root_dir):
        include_dirs.add(root_path)

    # 搜索所有的 .h 文件
    for h_file in glob.glob(os.path.join(root_dir, "**/*.h"), recursive=True):
        dir_path = os.path.dirname(h_file)

        # 转换为 VSCode 格式的路径
        rel_path = os.path.relpath(dir_path, root_dir)
        include_path = "${workspaceFolder}/" + rel_path.replace("\\", "/")

        # 应用排除规则
        if not should_exclude(include_path, root_dir):
            include_dirs.add(include_path)

    return sorted(list(include_dirs))

def update_cpp_properties(root_dir, include_dirs):
    """更新 c_cpp_properties.json 文件（保留原有路径，只添加新路径）"""
    cpp_props_path = os.path.join(root_dir, ".vscode", "c_cpp_properties.json")

    # 基础配置
    base_cpp_config = {
        "cStandard": C_STANDARD,
        "compilerPath": COMPILER_PATH,
        "cppStandard": CPP_STANDARD,
        "defines": DEFINES,
        "intelliSenseMode": INTELLISENSE_MODE,
        "name": CONFIG_NAME,
        "browse": {
            "limitSymbolsToIncludedHeaders": True
        }
    }

    # 记录本次实际新增的路径
    added_paths = []

    try:
        # 文件存在时的处理逻辑
        if os.path.exists(cpp_props_path):
            with open(cpp_props_path, 'r', encoding='utf-8') as f:
                cpp_props = json.load(f)

            # 寻找目标配置
            config_index = None
            for i, config in enumerate(cpp_props["configurations"]):
                if config["name"] == CONFIG_NAME:
                    config_index = i
                    break

            if config_index is not None:
                # 获取现有的includePath
                config = cpp_props["configurations"][config_index]
                existing_paths = config.get("includePath", [])

                # 仅保留真正路径（过滤掉注释标记）
                pure_existing_paths = [p for p in existing_paths if p != COMMENT_MARKER]

                # 计算新增路径（扫描到的且不在现有配置中的）
                new_paths = [p for p in include_dirs if p not in pure_existing_paths]
                added_paths = new_paths.copy()  # 记录本次新增

                if not new_paths:
                    print("\n没有发现需要添加的新路径，配置文件保持原样")
                    return

                # 构建更新后的路径列表
                if ADD_COMMENT_MARKER and new_paths:
                    # 如果配置中已有注释标记，则直接在其后添加新路径
                    if COMMENT_MARKER in existing_paths:
                        # 找到注释标记位置
                        marker_index = existing_paths.index(COMMENT_MARKER)
                        include_paths = (
                            existing_paths[:marker_index+1] +
                            new_paths +
                            existing_paths[marker_index+1:]
                        )
                    else:
                        # 添加新注释标记
                        include_paths = existing_paths + [COMMENT_MARKER] + new_paths
                else:
                    include_paths = existing_paths + new_paths

                # 创建更新后的配置
                updated_config = {**config, "includePath": include_paths}
                cpp_props["configurations"][config_index] = updated_config
            else:
                # 创建全新的配置
                if include_dirs:
                    added_paths = include_dirs.copy()

                    if ADD_COMMENT_MARKER:
                        include_paths = [COMMENT_MARKER] + include_dirs
                    else:
                        include_paths = include_dirs
                else:
                    include_paths = []

                updated_config = {**base_cpp_config, "includePath": include_paths}
                cpp_props["configurations"].append(updated_config)
        else:
            # 创建全新的文件
            os.makedirs(os.path.dirname(cpp_props_path), exist_ok=True)

            if include_dirs:
                added_paths = include_dirs.copy()

                if ADD_COMMENT_MARKER:
                    include_paths = [COMMENT_MARKER] + include_dirs
                else:
                    include_paths = include_dirs
            else:
                include_paths = []

            updated_config = {**base_cpp_config, "includePath": include_paths}
            cpp_props = {
                "configurations": [updated_config],
                "version": 4
            }

        # 保存文件
        with open(cpp_props_path, 'w', encoding='utf-8') as f:
            json.dump(cpp_props, f, indent=4, ensure_ascii=False)

        print(f"\n成功更新 {cpp_props_path}")

        # 显示新增路径统计
        if added_paths:
            print(f"本次新增 {len(added_paths)} 个路径:")
            for path in added_paths:
                print(f"  {path}")
        else:
            print("本次未添加任何新路径")

    except Exception as e:
        print(f"\n更新文件时出错: {str(e)}")
        import traceback
        traceback.print_exc()

##############################################################
# 主程序
##############################################################

if __name__ == "__main__":
    # 获取当前工作目录（假设是项目根目录）
    root_dir = os.path.abspath(os.path.dirname(__file__))

    print(f"正在搜索目录: {root_dir}")

    # 打印排除规则
    exclusion_rules = {
        "完全匹配排除": EXACT_MATCH_EXCLUDE,
        "相对路径格式排除": RELATIVE_PATH_EXCLUDE,
        "关键字排除": KEYWORD_EXCLUDE
    }

    if any(exclusion_rules.values()):
        print("\n应用排除规则:")
        for rule_type, rules in exclusion_rules.items():
            if rules:
                print(f"  {rule_type}:")
                for rule in rules:
                    print(f"    - {rule}")

    # 查找并处理包含目录
    include_dirs = find_include_dirs(root_dir)
    print(f"\n找到 {len(include_dirs)} 个包含目录（应用排除规则后）")

    # 更新配置文件
    update_cpp_properties(root_dir, include_dirs)
    print("\n完成!")
