/*
 * @Author: AI Assistant
 * @Date: 2025-01-27
 * @Description: 统一文件读取功能基类
 */

#ifndef GNN_FILE_READ_H_
#define GNN_FILE_READ_H_

#include "common/common.h"
#include "common/define.h"
#include "common/packet.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
namespace GNN
{

  // 从文件名中提取row和col数字，用于排序
  // 文件名格式: layer_0_{proj_name}_weights_wanda_bitmap_1d_row_{row}_{col}.txt
  static std::pair<uint32_t, uint32_t> extractRowColFromFileName(const std::string& file_name)
  {
    // 查找 "_row_" 的位置
    size_t row_pos = file_name.find("_row_");
    if (row_pos == std::string::npos)
    {
      return std::make_pair(0, 0);
    }

    // 从 "_row_" 后面开始查找
    size_t   start       = row_pos + 5;  // "_row_" 的长度是5
    size_t   end         = start;
    uint64_t total_words = 0;
    // 提取row数字
    while (end < file_name.length() && std::isdigit(file_name[end]))
    {
      end++;
    }
    if (end == start)
    {
      return std::make_pair(0, 0);
    }
    uint32_t row = std::stoul(file_name.substr(start, end - start));

    // 跳过下划线，提取col数字
    if (end >= file_name.length() || file_name[end] != '_')
    {
      return std::make_pair(row, 0);
    }
    start = end + 1;
    end   = start;

    while (end < file_name.length() && std::isdigit(file_name[end]))
    {
      end++;
    }
    if (end == start)
    {
      return std::make_pair(row, 0);
    }
    uint32_t col = std::stoul(file_name.substr(start, end - start));

    return std::make_pair(row, col);
  }

  // 文件名排序比较函数：先按row排序，再按col排序
  static bool compareFileNameByRowCol(const std::string& a, const std::string& b)
  {
    auto rowcol_a = extractRowColFromFileName(a);
    auto rowcol_b = extractRowColFromFileName(b);

    if (rowcol_a.first != rowcol_b.first)
    {
      return rowcol_a.first < rowcol_b.first;
    }
    return rowcol_a.second < rowcol_b.second;
  }
  struct readDataResult
  {
    std::vector<uint16_t> data;
    uint32_t              current_vertex_num;
  };
  // 文件读取基类
  class FileReader
  {
  protected:
    std::string data_file_base_path_;
    std::string data_file_suffix_;
    uint64_t    total_inst_num_cfg_;
    uint64_t    total_slice_num_cfg_;

  public:
    FileReader(uint32_t total_slice_num_cfg, uint32_t total_inst_num_cfg)
      : data_file_base_path_("./data/"), data_file_suffix_(".txt"),
        total_inst_num_cfg_(total_inst_num_cfg), total_slice_num_cfg_(total_slice_num_cfg)
    {
    }
    virtual ~FileReader() = default;

    // 设置数据文件路径模板
    void setDataFilePathTemplate(const std::string& base_path,
                                 const std::string& file_suffix = ".txt")
    {
      data_file_base_path_ = base_path;
      data_file_suffix_    = file_suffix;
    }

    // 构建文件路径
    std::string
    buildFilePath(int slice_index, int inst_index, const std::string& final_path = "") const
    {
      return data_file_base_path_ + std::to_string(slice_index) + "_inst_" +
             std::to_string(inst_index) + final_path + data_file_suffix_;
    }

    // 验证文件是否存在
    bool validateFile(const std::string& file_path) const
    {
      std::ifstream file(file_path);
      return file.good();
    }

    readDataResult readBitmapData(uint64_t inst_index) const
    {
      std::string current_file =
        data_file_base_path_ + std::to_string(inst_index / total_slice_num_cfg_) + "_" +
        std::to_string(inst_index % total_slice_num_cfg_) + data_file_suffix_;
      readDataResult result;
      if (inst_index > total_inst_num_cfg_)
      {
        D_ERROR("FILE_READ", "Inst index out of range: %d", inst_index);
        assert(false);
      }
      std::ifstream MK_col(current_file);
      std::string   line, temp;
      std::getline(MK_col, line);
      std::stringstream ss(line);
      uint32_t          current_vertex_num = std::count(line.begin(), line.end(), ' ') + 1;
      result.current_vertex_num            = current_vertex_num;
      std::vector<storage_t> out(current_vertex_num);
      for (uint16_t i = 0; i < current_vertex_num; i++)
      {
        std::getline(ss, temp, ' ');
        // 检查字符串是否为空或包含无效字符
        if (temp.empty())
        {
          D_ERROR("FILE_READ", "Empty string at position %d", i);
          continue;
        }
        // 检查是否只包含0和1
        if (temp.find_first_not_of("01") != std::string::npos)
        {
          D_ERROR("FILE_READ", "Invalid binary string at position %d: %s", i, temp.c_str());
          continue;
        }
        try
        {
          out[i] = (storage_t)stoi(temp, nullptr, 2);  // 使用基数为2（二进制）
        }
        catch (const std::invalid_argument& e)
        {
          D_ERROR("FILE_READ",
                  "Invalid argument at position %d: %s, error: %s",
                  i,
                  temp.c_str(),
                  e.what());
          out[i] = 0;  // 设置默认值
        }
        catch (const std::out_of_range& e)
        {
          D_ERROR(
            "FILE_READ", "Out of range at position %d: %s, error: %s", i, temp.c_str(), e.what());
          out[i] = 0;  // 设置默认值
        }
      }
      MK_col.close();
      result.data = out;
      return result;
    }

    std::vector<storage_t> readBitmapALLROWData(uint64_t inst_index) const
    {
      std::string current_file =
        data_file_base_path_ + std::to_string(inst_index / total_slice_num_cfg_) + "_" +
        std::to_string(inst_index % total_slice_num_cfg_) + data_file_suffix_;
      D_INFO("FILE_READ",
             "Reading file: %s (inst_index=%d, total_slice_num_cfg_=%d)",
             current_file.c_str(),
             inst_index,
             total_slice_num_cfg_);

      if (inst_index > total_inst_num_cfg_)
      {
        D_ERROR("DMA", "Inst index out of range: %d", inst_index);
        assert(false);
      }
      std::ifstream MK_col(current_file);
      if (!MK_col.is_open())
      {
        D_ERROR("FILE_READ", "Cannot open file: %s", current_file.c_str());
        return std::vector<storage_t>();  // 返回空向量
      }

      std::string            line, temp;
      std::vector<storage_t> out;
      // 读取文件的所有行
      while (std::getline(MK_col, line))
      {
        if (line.empty())
          continue;  // 跳过空行

        std::stringstream ss(line);
        // 计算当前行的数据个数
        uint32_t          line_data_count = std::count(line.begin(), line.end(), ' ') + 1;

        // 读取当前行的所有数据
        for (uint16_t i = 0; i < line_data_count; i++)
        {
          std::getline(ss, temp, ' ');
          // 检查字符串是否为空或包含无效字符
          if (temp.empty())
          {
            D_ERROR("FILE_READ", "Empty string at line, position %d", i);
            continue;
          }
          // 检查是否只包含0和1
          if (temp.find_first_not_of("01") != std::string::npos)
          {
            D_ERROR("FILE_READ", "Invalid binary string at line, position %d: %s", i, temp.c_str());
            continue;
          }
          try
          {
            out.push_back((storage_t)stoi(temp, nullptr, 2));  // 使用基数为2（二进制）
          }
          catch (const std::invalid_argument& e)
          {
            D_ERROR("FILE_READ",
                    "Invalid argument at line, position %d: %s, error: %s",
                    i,
                    temp.c_str(),
                    e.what());
            out.push_back(0);  // 设置默认值
          }
          catch (const std::out_of_range& e)
          {
            D_ERROR("FILE_READ",
                    "Out of range at line, position %d: %s, error: %s",
                    i,
                    temp.c_str(),
                    e.what());
            out.push_back(0);  // 设置默认值
          }
        }
      }

      D_INFO("FILE_READ", "Read %d total data points from file", out.size());
      MK_col.close();
      return out;
    }

    // 读取指定文件夹中的所有txt文件
    std::vector<std::vector<storage_t>> readFolderData(const std::string& folder_path) const
    {
      std::vector<std::vector<storage_t>> all_data;
      DIR*                                dir = opendir(folder_path.c_str());
      if (dir == nullptr)
      {
        D_ERROR("FILE_READ", "Cannot open directory: %s", folder_path.c_str());
        return all_data;
      }

      std::vector<std::string> file_names;
      struct dirent*           entry;
      while ((entry = readdir(dir)) != nullptr)
      {
        std::string file_name = entry->d_name;
        // 只处理.txt文件
        if (file_name.length() > 4 && file_name.substr(file_name.length() - 4) == ".txt")
        {
          // 只保留文件名开头是 q / k / v 的
          // const bool starts_with_q = (file_name.rfind("q", 0) == 0);
          // const bool starts_with_k = (file_name.rfind("k", 0) == 0);
          // const bool starts_with_v = (file_name.rfind("v", 0) == 0);

          // if (starts_with_q || starts_with_k || starts_with_v)
          // {
          //   file_names.push_back(file_name);
          // }
          file_names.push_back(file_name);
        }
      }
      closedir(dir);

      // 对文件名进行排序，按照row_col的数值顺序排序（0_0, 0_1, 0_2, ..., 0_10, ...）
      std::sort(file_names.begin(), file_names.end(), compareFileNameByRowCol);

      // 读取每个文件
      for (const auto& file_name : file_names)
      {
        std::string            full_path = folder_path + "/" + file_name;
        std::vector<storage_t> file_data = readBitmapALLROWDataFromFile(full_path);

        if (!file_data.empty())
        {
          all_data.push_back(file_data);
          D_INFO("FILE_READ", "Read file: %s, data size: %d", file_name.c_str(), file_data.size());
        }
      }

      D_INFO("FILE_READ", "Read %d files from folder: %s", all_data.size(), folder_path.c_str());
      return all_data;
    }

    // 从指定文件路径读取所有行的数据
    std::vector<storage_t> readBitmapALLROWDataFromFile(const std::string& file_path) const
    {
      D_INFO("FILE_READ", "Reading file: %s", file_path.c_str());
      std::ifstream MK_col(file_path);
      if (!MK_col.is_open())
      {
        D_ERROR("FILE_READ", "Cannot open file: %s", file_path.c_str());
        return std::vector<storage_t>();  // 返回空向量
      }
      std::string            line, temp;
      std::vector<storage_t> out;

      // 读取文件的所有行
      while (std::getline(MK_col, line))
      {
        if (line.empty())
          continue;  // 跳过空行

        std::stringstream ss(line);
        // 计算当前行的数据个数
        uint32_t          line_data_count = std::count(line.begin(), line.end(), ' ') + 1;

        // 读取当前行的所有数据
        for (uint16_t i = 0; i < line_data_count; i++)
        {
          std::getline(ss, temp, ' ');
          // 检查字符串是否为空或包含无效字符
          if (temp.empty())
          {
            D_ERROR("FILE_READ", "Empty string at line, position %d", i);
            continue;
          }
          // 检查是否只包含0和1
          if (temp.find_first_not_of("01") != std::string::npos)
          {
            D_ERROR("FILE_READ", "Invalid binary string at line, position %d: %s", i, temp.c_str());
            continue;
          }
          try
          {
            out.push_back((storage_t)stoi(temp, nullptr, 2));  // 使用基数为2（二进制）
          }
          catch (const std::invalid_argument& e)
          {
            D_ERROR("FILE_READ",
                    "Invalid argument at line, position %d: %s, error: %s",
                    i,
                    temp.c_str(),
                    e.what());
            out.push_back(0);  // 设置默认值
          }
          catch (const std::out_of_range& e)
          {
            D_ERROR("FILE_READ",
                    "Out of range at line, position %d: %s, error: %s",
                    i,
                    temp.c_str(),
                    e.what());
            out.push_back(0);  // 设置默认值
          }
        }
      }

      D_INFO("FILE_READ", "Read %d total data points from file", out.size());
      MK_col.close();
      return out;
    }

    // 读取layer_0文件夹下所有子文件夹的数据
    std::map<std::string, std::vector<std::vector<storage_t>>>
    readLayer0AllFolders(const std::string& layer0_path) const
    {
      std::map<std::string, std::vector<std::vector<storage_t>>> folder_data_map;

      DIR* dir = opendir(layer0_path.c_str());
      if (dir == nullptr)
      {
        D_ERROR("FILE_READ", "Cannot open layer_0 directory: %s", layer0_path.c_str());
        return folder_data_map;
      }

      std::vector<std::string> folder_names;
      struct dirent*           entry;
      while ((entry = readdir(dir)) != nullptr)
      {
        std::string entry_name = entry->d_name;
        // 跳过 . 和 ..
        if (entry_name == "." || entry_name == "..")
        {
          continue;
        }
        // 检查是否是目录（通过尝试打开）
        std::string full_path = layer0_path + "/" + entry_name;
        DIR*        sub_dir   = opendir(full_path.c_str());
        if (sub_dir != nullptr)
        {
          closedir(sub_dir);
          folder_names.push_back(entry_name);
        }
      }

      closedir(dir);

      // 对文件夹名进行排序
      std::sort(folder_names.begin(), folder_names.end());

      // 读取每个子文件夹
      for (const auto& folder_name : folder_names)
      {
        std::string full_folder_path = layer0_path + "/" + folder_name;
        D_INFO("FILE_READ", "Reading folder: %s", full_folder_path.c_str());
        std::vector<std::vector<storage_t>> folder_data = readFolderData(full_folder_path);
        if (!folder_data.empty())
        {
          folder_data_map[folder_name] = folder_data;
          D_INFO("FILE_READ",
                 "Successfully read folder: %s, %d files",
                 folder_name.c_str(),
                 folder_data.size());
        }
      }

      D_INFO("FILE_READ", "Read %d folders from layer_0", folder_data_map.size());
      return folder_data_map;
    }
  };
}  // namespace GNN

#endif  // GNN_FILE_READ_H_
