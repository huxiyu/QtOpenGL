#include "openglshaderprogram.h"
#include <QDir>
#include <QFile>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <OpenGLFunctions>
#include <OpenGLUniformBufferObject>
#include <OpenGLSLParser>
#include <OpenGLUniformBufferManager>

#include <string>

#include "kabstractlexer.h"
#include "kbufferedfilereader.h"
#include "kcommon.h"
#include "kparsetoken.h"
#include "kstringwriter.h"

class OpenGLShaderProgramWrappedPrivate
{
public:
  std::vector<char const*> m_includePaths;
  std::vector<std::string> m_autoresolver;
};

/*******************************************************************************
 * OpenGLShaderProgramWrapped
 ******************************************************************************/
OpenGLShaderProgramWrapped::OpenGLShaderProgramWrapped(QObject *parent) :
  OpenGLShaderProgramChecked(parent), m_private(new OpenGLShaderProgramWrappedPrivate)
{
  // Intentionally Empty
}

OpenGLShaderProgramWrapped::~OpenGLShaderProgramWrapped()
{
  delete m_private;
}

void OpenGLShaderProgramWrapped::addIncludePath(const char *path)
{
  m_private->m_includePaths.push_back(path);
}

void OpenGLShaderProgramWrapped::addSharedIncludePath(const char *path)
{
  OpenGLSLParser::addSharedIncludePath(path);
}

bool OpenGLShaderProgramWrapped::addShaderFromSourceFile(QOpenGLShader::ShaderType type, const QString &fileName)
{
  P(OpenGLShaderProgramWrappedPrivate);
  std::string ppSource = getVersionComment().toUtf8().constData();

  // Preprocess the shader file
  KBufferedFileReader reader(fileName, 1024);

  if (!reader.valid())
  {
    qFatal("Failed to open file: `%s`", qPrintable(fileName));
  }

  KStringWriter writer(ppSource);
  OpenGLSLParser parser(&reader, &writer);
  parser.setFilePath(fileName.toUtf8().constData());
  for (char const *path : m_private->m_includePaths)
  {
    parser.addIncludePath(path);
  }
  parser.setAutoresolver(&p.m_autoresolver);
  parser.initialize();
  if (parser.parse())
  {
    return OpenGLShaderProgramChecked::addShaderFromSourceCode(type, ppSource.c_str());
  }
  return false;
}

void OpenGLShaderProgramWrapped::uniformBlockBinding(const char *location, const OpenGLUniformBufferObject &ubo)
{
  OpenGLFunctions f;
  f.initializeOpenGLFunctions();
  GLuint index = this->uniformBlockLocation(location);
  this->uniformBlockBinding(index, ubo);
}

void OpenGLShaderProgramWrapped::uniformBlockBinding(unsigned location, const OpenGLUniformBufferObject &ubo)
{
  OpenGLFunctions f;
  f.initializeOpenGLFunctions();
  f.glBindBufferBase(GL_UNIFORM_BUFFER, ubo.locationId(), ubo.bufferId());
  f.glUniformBlockBinding(this->programId(), location, ubo.locationId());
}

unsigned OpenGLShaderProgramWrapped::uniformBlockLocation(const char *location)
{
  OpenGLFunctions f;
  f.initializeOpenGLFunctions();
  return f.glGetUniformBlockIndex(this->programId(), location);
}

QString OpenGLShaderProgramWrapped::getVersionComment()
{
  QString comment = "#version ";
  QOpenGLContext *ctx = QOpenGLContext::currentContext();
  QSurfaceFormat format = ctx->format();
  QPair<int,int> version = format.version();

  // Concatenate version number
  // Note: Version number calculation is different based on GLES/GL and
  //       current context version number. It's a little more difficult than
  //       just concatenating some numbers together.
  if (ctx->isOpenGLES())
  {
    comment += QString::number(version.first) + QString::number(version.second) + "0 es";
  }
  else
  {
    switch (version.first)
    {
      case 2:
        switch (version.second)
        {
          case 0:
            comment += "110";
            break;
          case 1:
            comment += "120";
            break;
          default:
            comment += "120";
            break;
        }
        break;
      case 3:
        switch (version.second)
        {
          case 0:
            comment += "130";
            break;
          case 1:
            comment += "140";
            break;
          case 2:
            comment += "150";
            break;
          default:
            comment += QString::number(version.first) + QString::number(version.second) + "0";
            break;
        }
        break;
      default:
        comment += QString::number(version.first) + QString::number(version.second) + "0";
        break;
    }
  }

  return comment + "\n";
}

bool OpenGLShaderProgramWrapped::link()
{
  P(OpenGLShaderProgramWrappedPrivate);
  bool ret = OpenGLShaderProgramChecked::link();
  if (!p.m_autoresolver.empty())
  {
    bind();
    for (std::string const &resolver : p.m_autoresolver)
    {
      unsigned loc = uniformBlockLocation(resolver.c_str());
      if (loc == OpenGLUniformBufferObject::InvalidLocation)
      {
        qFatal("Failed to find the UBO `%s` to for autoresolve.", resolver.c_str());
        return false;
      }
      OpenGLUniformBufferObject *ubo = OpenGLUniformBufferManager::find(resolver.c_str());
      if (!ubo)
      {
        qFatal("Manager has no knowledge of the UBO `%s`.", resolver.c_str());
        return false;
      }
      uniformBlockBinding(loc, *ubo);
    }
    release();
  }
  return ret;
}
