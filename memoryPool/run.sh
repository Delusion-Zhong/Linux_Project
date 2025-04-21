#!/bin/bash

# 设置错误时退出
set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印带颜色的信息
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查并添加执行权限
check_permission() {
    if [ ! -x "$0" ]; then
        print_warning "脚本没有执行权限，正在添加执行权限..."
        chmod +x "$0"
        print_info "执行权限已添加，请重新运行脚本"
        exit 0
    fi
}

# 清理函数
cleanup() {
    print_info "清理构建目录..."
    rm -rf build
}

# 检查命令是否存在
check_command() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 未安装，请先安装 $1"
        exit 1
    fi
}

# 检查执行权限
check_permission

# 检查必要的命令
check_command cmake
check_command make



# 创建并进入build目录
print_info "创建构建目录..."
mkdir -p build
cd build

# 配置项目
print_info "配置项目..."
cmake ..

# 编译项目
print_info "开始编译..."
make -j$(nproc)

# 运行测试
print_info "运行测试..."
./memory_pool_test

# 检查测试结果
if [ $? -eq 0 ]; then
    print_info "测试运行成功！"
else
    print_error "测试运行失败！"
    exit 1
fi
