/*
 *  Copyright (C) 2012-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

// Android apk file i/o. Depends on libzip
// Basically the same format as zip.
// We might want to refactor CFileZip someday...
//////////////////////////////////////////////////////////////////////

#include "APKFile.h"

#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <zip.h>

using namespace XFILE;

class CAPKFile {
private:
    static constexpr size_t CACHE_BUFFER_SIZE = 8192;
    std::vector<uint8_t> m_cache_buffer;
    int64_t m_cache_position = 0;
    
    bool FillCache(int64_t position) {
        m_cache_buffer.resize(CACHE_BUFFER_SIZE);
        int64_t bytes_read = zip_fread(m_zip_file, m_cache_buffer.data(), CACHE_BUFFER_SIZE);
        if (bytes_read > 0) {
            m_cache_position = position;
            m_cache_buffer.resize(bytes_read);
            return true;
        }
        return false;
    }
};

CAPKFile::CAPKFile()
{
  m_file_pos    = 0;
  m_file_size   = 0;
  m_zip_index   =-1;
  m_zip_file    = NULL;
  m_zip_archive = NULL;
}

bool CAPKFile::Open(const CURL& url)
{
  Close();

  m_url = url;
  const std::string& path = url.GetFileName();
  const std::string& host = url.GetHostName();

  int zip_flags = 0, zip_error = 0;
  m_zip_archive = zip_open(host.c_str(), zip_flags, &zip_error);
  if (!m_zip_archive || zip_error)
  {
    CLog::Log(LOGERROR, "CAPKFile::Open: Unable to open archive : '{}'", host);
    return false;
  }

  m_zip_index = zip_name_locate(m_zip_archive, path.c_str(), zip_flags);
  if (m_zip_index == -1)
  {
    // might not be an error if caller is just testing for presence/absence
    CLog::Log(LOGDEBUG, "CAPKFile::Open: Unable to locate file : '{}'", path);
    zip_close(m_zip_archive);
    m_zip_archive = NULL;
    return false;
  }

  // cache the file size
  struct zip_stat sb;
  zip_stat_init(&sb);
  int rtn = zip_stat_index(m_zip_archive, m_zip_index, zip_flags, &sb);
  if (rtn == -1)
  {
    CLog::Log(LOGERROR, "CAPKFile::Open: Unable to stat file : '{}'", path);
    zip_close(m_zip_archive);
    m_zip_archive = NULL;
    return false;
  }
  m_file_pos = 0;
  m_file_size = sb.size;

  // finally open the file
  m_zip_file = zip_fopen_index(m_zip_archive, m_zip_index, zip_flags);
  if (!m_zip_file)
  {
    CLog::Log(LOGERROR, "CAPKFile::Open: Unable to open file : '{}'", path);
    zip_close(m_zip_archive);
    m_zip_archive = NULL;
    return false;
  }

  // We've successfully opened the file!
  return true;
}

bool CAPKFile::Exists(const CURL& url)
{
  struct __stat64 buffer;
  return (Stat(url, &buffer) == 0);
}

void CAPKFile::Close()
{
  if (m_zip_archive)
  {
    if (m_zip_file)
      zip_fclose(m_zip_file);
    m_zip_file  = NULL;
  }
  zip_close(m_zip_archive);
  m_zip_archive = NULL;
  m_file_pos    = 0;
  m_file_size   = 0;
  m_zip_index   =-1;
}

int64_t CAPKFile::Seek(int64_t iFilePosition, int iWhence)
{
  // libzip has no seek so we have to fake it with reads
  off64_t file_pos = -1;
  if (m_zip_archive && m_zip_file)
  {
    switch(iWhence)
    {
      default:
      case SEEK_CUR:
        // set file offset to current plus offset
        if (m_file_pos + iFilePosition > m_file_size)
          return -1;
        file_pos = m_file_pos + iFilePosition;
        break;

      case SEEK_SET:
        // set file offset to offset
        if (iFilePosition > m_file_size)
          return -1;
        file_pos = iFilePosition;
        break;

      case SEEK_END:
        // set file offset to EOF minus offset
        if (iFilePosition > m_file_size)
          return -1;
        file_pos = m_file_size - iFilePosition;
        break;
    }
    // if offset is past current file position
    // then we must close, open then seek from zero.
    if (file_pos < m_file_pos)
    {
      zip_fclose(m_zip_file);
      int zip_flags = 0;
      m_zip_file = zip_fopen_index(m_zip_archive, m_zip_index, zip_flags);
    }
    char buffer[1024];
    int read_bytes = 1024 * (file_pos / 1024);
    for (int i = 0; i < read_bytes; i += 1024)
      zip_fread(m_zip_file, buffer, 1024);
    if (file_pos - read_bytes > 0)
      zip_fread(m_zip_file, buffer, file_pos - read_bytes);
    m_file_pos = file_pos;
  }

  return m_file_pos;
}

ssize_t CAPKFile::Read(void *lpBuf, size_t uiBufSize)
{
  if (uiBufSize > SSIZE_MAX)
    uiBufSize = SSIZE_MAX;

  ssize_t bytes_read = uiBufSize;
  if (m_zip_archive && m_zip_file)
  {
    // check for a read pas EOF and clamp it to EOF
    if ((m_file_pos + bytes_read) > m_file_size)
      bytes_read = m_file_size - m_file_pos;

    bytes_read = zip_fread(m_zip_file, lpBuf, bytes_read);
    if (bytes_read != -1)
      m_file_pos += bytes_read;
    else
      bytes_read = 0;
  }

  return bytes_read;
}

int CAPKFile::Stat(struct __stat64* buffer)
{
  return Stat(m_url, buffer);
}

int CAPKFile::Stat(const CURL& url, struct __stat64* buffer)
{
  memset(buffer, 0, sizeof(struct __stat64));

  // do not use internal member vars here,
  //  we might be called without opening
  std::string path = url.GetFileName();
  const std::string& host = url.GetHostName();

  struct zip *zip_archive;
  int zip_flags = 0, zip_error = 0;
  zip_archive = zip_open(host.c_str(), zip_flags, &zip_error);
  if (!zip_archive || zip_error)
  {
    CLog::Log(LOGERROR, "CAPKFile::Stat: Unable to open archive : '{}'", host);
    errno = ENOENT;
    return -1;
  }

  // check if file exists
  int zip_index = zip_name_locate(zip_archive, url.GetFileName().c_str(), zip_flags);
  if (zip_index != -1)
  {
    struct zip_stat sb;
    zip_stat_init(&sb);
    int rtn = zip_stat_index(zip_archive, zip_index, zip_flags, &sb);
    if (rtn != -1)
    {
      buffer->st_gid  = 0;
      buffer->st_size = sb.size;
      buffer->st_mode = _S_IFREG;
      buffer->st_atime = sb.mtime;
      buffer->st_ctime = sb.mtime;
      buffer->st_mtime = sb.mtime;
    }
  }

  // check if directory exists
  if (buffer->st_mode != _S_IFREG)
  {
    // zip directories have a '/' at end.
    if (!URIUtils::HasSlashAtEnd(path))
      URIUtils::AddSlashAtEnd(path);

    int numFiles = zip_get_num_files(zip_archive);
    for (int i = 0; i < numFiles; i++)
    {
      std::string name = zip_get_name(zip_archive, i, zip_flags);
      if (!name.empty() && URIUtils::PathHasParent(name, path))
      {
        buffer->st_gid  = 0;
        buffer->st_mode = _S_IFDIR;
        break;
      }
    }
  }
  zip_close(zip_archive);

  if (buffer->st_mode != 0)
  {
    errno = 0;
    return 0;
  }
  else
  {
    errno = ENOENT;
    return -1;
  }
}

int64_t CAPKFile::GetPosition()
{
  return m_file_pos;
}

int64_t CAPKFile::GetLength()
{
  return m_file_size;
}

int  CAPKFile::GetChunkSize()
{
  return 1;
}
