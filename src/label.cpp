#include "global.h"
#include "shaderfactory.h"
#include "staticfunctions.h"
#include "label.h"

#include <QtMath>
#include <QMessageBox>

Label::Label()
{
  m_caption.clear();
  m_position = Vec(0,0,0);
  m_proximity = -1;
  m_color = Vec(1,1,1);
  m_fontSize = 20;
  m_linkData.clear();

  m_treeInfo.clear();

  m_vertData = 0;
  m_boxData = 0;
  m_texWd = m_texHt = 0;

  m_hitDur = 0;
}

Label::~Label()
{
  m_caption.clear();
  m_position = Vec(0,0,0);
  m_proximity = -1;
  m_color = Vec(1,1,1);
  m_fontSize = 20;
  m_linkData.clear();
  m_treeInfo.clear();

  if(m_vertData)
    {
      delete [] m_vertData;
      glDeleteBuffers(1, &m_glIndexBuffer);
      glDeleteVertexArrays( 1, &m_glVertArray );
      glDeleteBuffers(1, &m_glVertBuffer);
      m_glIndexBuffer = 0;
      m_glVertArray = 0;
      m_glVertBuffer = 0;
    }
}

void
Label::setTreeInfo(QList<float> ti)
{
  m_treeInfo = ti;

  m_hitC = 0.5*m_color + 0.5*Vec(1,0.3,0);
  QColor color(m_color.z*255,m_color.y*255,m_color.x*255);
  
  int ht = 0;
  int wd = 0;
  QList<QImage> img;
  for(int i=0; i<m_treeInfo.count(); i++)
    {
      QString text;
      if (i == 0) text = QString("Height : %1").arg(m_treeInfo[i]);
      if (i == 1) text = QString("Area : %1").arg(m_treeInfo[i]);
      if (i == 2) text = QString("Point Count : %1").arg(m_treeInfo[i]);

      if (i == m_treeInfo.count()) text = QString("Tree Information");

      QFont font;
      if (i == -1) font = QFont("Helvetica", 48);
      else font = QFont("Helvetica", 30);

      QImage textimg = StaticFunctions::renderText(text,
						   font,
						   Qt::black,
						   Qt::white,
						   false);
      img << textimg;
      wd = qMax(wd, textimg.width());
      ht = ht + textimg.height();
    }

  m_texWd = wd+5;
  m_texHt = ht+5;

  QImage image = QImage(m_texWd, m_texHt, QImage::Format_ARGB32);
  image.fill(Qt::black);
  QPainter p(&image);
  p.setPen(QPen(Qt::gray, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawRoundedRect(1, 1, m_texWd-2, m_texHt-2, 5, 5);
  int cht = 3;
  for (int i=0; i<img.count(); i++)
    {
      int cwd = (m_texWd - img[i].width())/2;
      p.drawImage(cwd, cht, img[i].rgbSwapped());
      cht += img[i].height();
    }

  glGenTextures(1, &m_glTexture);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, m_glTexture);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); 
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D,
	       0,
	       4,
	       m_texWd,
	       m_texHt,
	       0,
	       GL_RGBA,
	       GL_UNSIGNED_BYTE,
	       image.bits());
  
  glDisable(GL_TEXTURE_2D);
}

void
Label::setGlobalMinMax(Vec gmin, Vec gmax)
{
  m_position -= gmin;
}

void
Label::drawLabel(Camera* cam)
{
  Vec cpos = cam->position();
    
  if ((cpos-m_position).norm() > m_proximity)
    return;

  if ((m_position-cpos)*cam->viewDirection() < 0)
    return;
  
  Vec scr = cam->projectedCoordinatesOf(m_position);
  int x = scr.x;
  int y = scr.y;
  
  float frc = 1.1-(cpos-m_position).norm()/m_proximity;
  int fsize = m_fontSize*frc;
  QFont font = QFont("Helvetica", fsize);

  QColor color(m_color.z*255,m_color.y*255,m_color.x*255);

  StaticFunctions::renderText(x, y, m_caption, font, Qt::black, color);

  if (!m_linkData.isEmpty())
    {
      float prox = (cpos-m_position).norm()/m_proximity;      
      Vec col = Vec(1.0-prox*0.5, prox, prox*0.5);
      col *= 255;
      QColor bcol(col.x, col.y, col.z);
      StaticFunctions::renderText(x, y+2*fsize, "^", font, bcol, color);
    }
}

void
Label::stickToGround()
{
  m_position = Global::stickToGround(m_position);
  createBox();
}

float
Label::checkHit(QMatrix4x4 matR,
		QMatrix4x4 finalxformInv,
		float deadRadius,
		QVector3D deadPoint)
{
  if (deadRadius <= 0)
    {
      // we are showing info icon
      QVector3D vp = QVector3D(m_position.x, m_position.y, m_position.z);
      QVector3D sz = QVector3D(0.002,0.002,0.002);
      QVector3D bmin = vp - sz;
      QVector3D bmax = vp + sz;
      
      QVector3D centerR = QVector3D(matR * QVector4D(0,0,0,1));
      QVector3D cenR = finalxformInv.map(centerR);
      
      QVector3D pinPoint = QVector3D(matR * QVector4D(0,0,-1,1));
      QVector3D frtR = finalxformInv.map(pinPoint) - cenR;
      frtR.normalize();
      
      return StaticFunctions::intersectRayBox(bmin, bmax,
					      cenR, frtR);
    }

  // we are showing boxes

  //if (deadRadius <= 0)
  //return -100000;

  Vec vp2d(m_position.x, m_position.y, 0);
  Vec dp2d(deadPoint.x(), deadPoint.y(), 0);
  
  if ((vp2d-dp2d).norm() > deadRadius-0.02) // return if not in the zone
    return -100000;

  
  QVector3D bmin = QVector3D(m_boxData[0],m_boxData[1],m_boxData[2]);
  QVector3D bmax = QVector3D(m_boxData[48],m_boxData[49],m_boxData[50]);

  QVector3D centerR = QVector3D(matR * QVector4D(0,0,0,1));
  QVector3D cenR = finalxformInv.map(centerR);

  QVector3D pinPoint = QVector3D(matR * QVector4D(0,0,-1,1));
  QVector3D frtR = finalxformInv.map(pinPoint) - cenR;
  frtR.normalize();

  return StaticFunctions::intersectRayBox(bmin, bmax,
					  cenR, frtR);
}

void
Label::genVertData()
{
  m_vertData = new float[32];
  memset(m_vertData, 0, sizeof(float)*32);


  // create and bind a VAO to hold state for this model
  glGenVertexArrays( 1, &m_glVertArray );
  glBindVertexArray( m_glVertArray );
      
  // Populate a vertex buffer
  glGenBuffers( 1, &m_glVertBuffer );
  glBindBuffer( GL_ARRAY_BUFFER, m_glVertBuffer );
  glBufferData( GL_ARRAY_BUFFER,
		sizeof(float)*8*30,
		NULL,
		GL_STATIC_DRAW );
      
  // Identify the components in the vertex buffer
  glEnableVertexAttribArray( 0 );
  glVertexAttribPointer( 0, //attribute 0
			 3, // size
			 GL_FLOAT, // type
			 GL_FALSE, // normalized
			 sizeof(float)*8, // stride
			 (void *)0 ); // starting offset

  glEnableVertexAttribArray( 1 );
  glVertexAttribPointer( 1,
			 3,
			 GL_FLOAT,
			 GL_FALSE,
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*3 );
  
  glEnableVertexAttribArray( 2 );
  glVertexAttribPointer( 2,
			 2,
			 GL_FLOAT,
			 GL_FALSE, 
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*6 );
  
  
  // Create and populate the index buffer
  glGenBuffers(1, &m_glIndexBuffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glIndexBuffer);
  
  if (m_treeInfo.count() > 0) // draw box
    {
      if (!m_boxData)
	createBox();
      
      glBindVertexArray(m_glVertArray);
      glBindBuffer( GL_ARRAY_BUFFER, m_glVertBuffer);
      glBufferSubData(GL_ARRAY_BUFFER,
		      0,
		      sizeof(float)*8*24,
		      &m_boxData[0]);
      
      uchar indexData[36];
      //	  for(int i=0; i<24; i++)
      //	    indexData[i] = i;
      
      for(int i=0; i<6; i++)
	{
	  indexData[6*i+0] = 4*i+0;
	  indexData[6*i+1] = 4*i+1;
	  indexData[6*i+2] = 4*i+2;
	  indexData[6*i+3] = 4*i+0;
	  indexData[6*i+4] = 4*i+2;
	  indexData[6*i+5] = 4*i+3;
	}
      
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		   sizeof(uchar)*36,
		   &indexData[0],
		   GL_STATIC_DRAW);
    }
  else
    {
      uchar indexData[6];
      indexData[0] = 0;
      indexData[1] = 1;
      indexData[2] = 2;
      indexData[3] = 0;
      indexData[4] = 2;
      indexData[5] = 3;
      
      glBufferData( GL_ELEMENT_ARRAY_BUFFER,
		    sizeof(uchar) * 2 * 3,
		    &indexData[0],
		    GL_STATIC_DRAW );
    }
  
  glBindVertexArray( 0 );
  
  
  
  int fsize = m_fontSize;
  if (!m_glTexture)
    {
      QFont font = QFont("Helvetica", fsize);
      QColor color(m_color.z*255,m_color.y*255,m_color.x*255); 
      QImage tmpTex = StaticFunctions::renderText(m_caption,
						  font,
						  Qt::black, color);      
      m_texWd = tmpTex.width();
      m_texHt = tmpTex.height();
      
      glGenTextures(1, &m_glTexture);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, m_glTexture);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); 
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexImage2D(GL_TEXTURE_2D,
		   0,
		   4,
		   m_texWd,
		   m_texHt,
		   0,
		   GL_RGBA,
		   GL_UNSIGNED_BYTE,
		   tmpTex.bits());
      
      glDisable(GL_TEXTURE_2D);
    }  
}

void
Label::drawLabel(QVector3D cpos,
		 QVector3D vDir,
		 QVector3D uDir,
		 QVector3D rDir,
		 QMatrix4x4 mvp,
		 QMatrix4x4 matR,
		 QMatrix4x4 finalxform,
		 QMatrix4x4 finalxformInv,
		 float deadRadius,
		 QVector3D deadPoint,
		 bool glow)
{
  if (!m_vertData)
    genVertData();
  

  QVector3D vp = QVector3D(m_position.x, m_position.y, m_position.z);

  if (m_treeInfo.count() > 0)
    {
      QVector3D centerR = QVector3D(matR * QVector4D(0,0,0,1));
      QVector3D frontR;
      frontR = QVector3D(matR * QVector4D(0,0,-0.1,1)) - centerR;
      QVector3D pinPoint = centerR + frontR;
      QVector3D fxvp = finalxform.map(vp);

      if (fxvp.distanceToLine(pinPoint, frontR) > 0.02)
	{
	  Vec vp2d(vp.x(), vp.y(), 0);
	  Vec dp2d(deadPoint.x(), deadPoint.y(), 0);
	  
	  if (deadRadius <= 0 ||
	      (vp2d-dp2d).norm() > deadRadius-0.02) // take slightly smaller radius
	    {
	      // show icon only if we are close
	      if ((cpos-vp).lengthSquared() < 1)
		showTreeInfoPosition(mvp, glow);
	    }
	  else
	    drawBox(mvp, vDir, glow, true);
	  
	  return;
	}
    }
  else if ((cpos-vp).length() > m_proximity)
    return;


  //glDepthMask(GL_FALSE); // disable writing to depth buffer
  //glDisable(GL_DEPTH_TEST);


  QVector3D nDir = (cpos-vp).normalized();
  QVector3D uD = QVector3D::crossProduct(nDir, rDir);
  uD.normalized();
  QVector3D rD = QVector3D::crossProduct(nDir, uD);
  rD.normalized();

  float frc = 1.0/qMax(m_texWd, m_texHt);

  float projFactor = 0.045f/qTan(qDegreesToRadians(55.0));
  float dist = (cpos-vp).length();
  float dpf = projFactor/dist;
  frc = frc * 0.01/dpf;


  QVector3D vu = frc*m_texHt*uD;
  QVector3D vr0 = vp - frc*m_texWd*0.5*rD;
  QVector3D vr1 = vp + frc*m_texWd*0.5*rD;

  QVector3D v0 = vr0;
  QVector3D v1 = vr0 - vu;
  QVector3D v2 = vr1 - vu;
  QVector3D v3 = vr1;

  m_vertData[0] = v0.x();
  m_vertData[1] = v0.y();
  m_vertData[2] = v0.z();
  m_vertData[3] = vDir.x();
  m_vertData[4] = vDir.y();
  m_vertData[5] = vDir.z();
  m_vertData[6] = 0.0;
  m_vertData[7] = 0.0;

  m_vertData[8] = v1.x();
  m_vertData[9] = v1.y();
  m_vertData[10] = v1.z();
  m_vertData[11] = vDir.x();
  m_vertData[12] = vDir.y();
  m_vertData[13] = vDir.z();
  m_vertData[14] = 0.0;
  m_vertData[15] = 1.0;

  m_vertData[16] = v2.x();
  m_vertData[17] = v2.y();
  m_vertData[18] = v2.z();
  m_vertData[19] = vDir.x();
  m_vertData[20] = vDir.y();
  m_vertData[21] = vDir.z();
  m_vertData[22] = 1.0;
  m_vertData[23] = 1.0;

  m_vertData[24] = v3.x();
  m_vertData[25] = v3.y();
  m_vertData[26] = v3.z();
  m_vertData[27] = vDir.x();
  m_vertData[28] = vDir.y();
  m_vertData[29] = vDir.z();
  m_vertData[30] = 1.0;
  m_vertData[31] = 0.0;

//  glEnable(GL_BLEND);
//  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);


  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, m_glTexture);
  glEnable(GL_TEXTURE_2D);

  glBindVertexArray(m_glVertArray);
  glBindBuffer( GL_ARRAY_BUFFER, m_glVertBuffer);
  glBufferSubData(GL_ARRAY_BUFFER,
		  0,
		  sizeof(float)*8*4,
		  &m_vertData[0]);

  // Identify the components in the vertex buffer
  glEnableVertexAttribArray( 0 );
  glVertexAttribPointer( 0, //attribute 0
			 3, // size
			 GL_FLOAT, // type
			 GL_FALSE, // normalized
			 sizeof(float)*8, // stride
			 (void *)0 ); // starting offset
  
  glEnableVertexAttribArray( 1 );
  glVertexAttribPointer( 1,
			 3,
			 GL_FLOAT,
			 GL_FALSE,
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*3 );
  
  glEnableVertexAttribArray( 2 );
  glVertexAttribPointer( 2,
			 2,
			 GL_FLOAT,
			 GL_FALSE, 
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*6 );
  
  glUseProgram(ShaderFactory::rcShader());
  GLint *rcShaderParm = ShaderFactory::rcShaderParm();
  glUniformMatrix4fv(rcShaderParm[0], 1, GL_FALSE, mvp.data() );  
  glUniform1i(rcShaderParm[1], 4); // texture
  glUniform3f(rcShaderParm[2], 0, 0, 0); // color
  glUniform3f(rcShaderParm[3], 0, 0, 0); // view direction
  glUniform1f(rcShaderParm[4], 0.5); // opacity modulator
  glUniform1i(rcShaderParm[5], 2); // applytexture
  glUniform1f(rcShaderParm[6], 5); // pointsize
  glUniform1f(rcShaderParm[7], 0.5); // mix color

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glIndexBuffer);  
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0); 
  glBindVertexArray(0);

  glDisable(GL_TEXTURE_2D);

  glUseProgram( 0 );

//  glDisable(GL_BLEND);
  //glDepthMask(GL_TRUE); // enable writing to depth buffer
  //glEnable(GL_DEPTH_TEST);

//  if (!m_linkData.isEmpty())
//    {
//      float prox = (cpos-pos).length()/m_proximity;      
//      Vec col = Vec(1.0-prox*0.5, prox, prox*0.5);
//      col *= 255;
//      QColor bcol(col.x, col.y, col.z);
//      StaticFunctions::renderText(x, y+2*fsize, "^", font, bcol, color);
//    }
}

bool
Label::checkLink(Camera *cam, QPoint pos)
{
  Vec cpos = cam->position();
    
  if ((cpos-m_position).norm() > m_proximity)
    return false;

  if ((m_position-cpos)*cam->viewDirection() < 0)
    return false;
  
  Vec scr = cam->projectedCoordinatesOf(m_position);
  
  float frc = 1.1-(cpos-m_position).norm()/m_proximity;
  int fsize = m_fontSize*frc;

  if ( qAbs(pos.x()-scr.x) < fsize &&
       qAbs(pos.y()-scr.y) < fsize)
    return true;

  return false;
}

void
Label::showTreeInfoPosition(QMatrix4x4 mvp, bool glow)
{
  glDepthMask(GL_FALSE); // disable writing to depth buffer

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  m_vertData[0] = m_position.x;
  m_vertData[1] = m_position.y;
  m_vertData[2] = m_position.z;
  m_vertData[3] = 0;
  m_vertData[4] = 0;
  m_vertData[5] = 0;
  m_vertData[6] = 0;
  m_vertData[7] = 0;

  glEnable(GL_PROGRAM_POINT_SIZE );
  glEnable(GL_POINT_SPRITE);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, Global::infoSpriteTexture());
  glEnable(GL_TEXTURE_2D);

  glBindVertexArray(m_glVertArray);
  glBindBuffer( GL_ARRAY_BUFFER, m_glVertBuffer);
  glBufferSubData(GL_ARRAY_BUFFER,
		  0,
		  sizeof(float)*8,
		  &m_vertData[0]);

  // Identify the components in the vertex buffer
  glEnableVertexAttribArray( 0 );
  glVertexAttribPointer( 0, //attribute 0
			 3, // size
			 GL_FLOAT, // type
			 GL_FALSE, // normalized
			 sizeof(float)*8, // stride
			 (void *)0 ); // starting offset
  
  glEnableVertexAttribArray( 1 );
  glVertexAttribPointer( 1,
			 3,
			 GL_FLOAT,
			 GL_FALSE,
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*3 );
  
  glEnableVertexAttribArray( 2 );
  glVertexAttribPointer( 2,
			 2,
			 GL_FLOAT,
			 GL_FALSE, 
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*6 );

  glUseProgram(ShaderFactory::rcShader());
  GLint *rcShaderParm = ShaderFactory::rcShaderParm();
  glUniformMatrix4fv(rcShaderParm[0], 1, GL_FALSE, mvp.data() );  
  glUniform1i(rcShaderParm[1], 4); // texture

  glUniform3f(rcShaderParm[3], 0, 0, 0); // view direction
  glUniform1i(rcShaderParm[5], 3); // point sprite

  if (!glow)
    {
      glUniform3f(rcShaderParm[2], 0, 0, 0); // color
      glUniform1f(rcShaderParm[4], 0.8); // opacity modulator
      glUniform1f(rcShaderParm[6], 20); // pointsize
      glUniform1f(rcShaderParm[7], 0); // mix color
    }
  else
    {
      glUniform3f(rcShaderParm[2], 1, 0.3, 0.0); // mix color
      glUniform1f(rcShaderParm[4], 1); // opacity modulator
      glUniform1f(rcShaderParm[6], 40); // pointsize
      glUniform1f(rcShaderParm[7], 0.8); // mix color
    }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glIndexBuffer);  
  glDrawArrays(GL_POINTS, 0, 1);  
  glBindVertexArray(0);

  glDisable(GL_PROGRAM_POINT_SIZE );
  glDisable(GL_POINT_SPRITE);
  glDisable(GL_TEXTURE_2D);

  glUseProgram( 0 );

  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE); // enable writing to depth buffer
}

void
Label::createBox()
{
  // use area parameter for side length of box
  float sz = qSqrt(m_treeInfo[1])/2;
  //float sz = qSqrt(m_treeInfo[1]);

  Vec bmin, bmax;
  bmin = Vec(-sz, -sz, 0);
  bmax = Vec(sz, sz, m_treeInfo[0]);

  bmin *= 0.01;
  bmax *= 0.01;

  bmin += m_position;
  bmax += m_position;
 
  if (!m_boxData)
    m_boxData = new float[24*8];

  QList<Vec> box; 
  box << Vec(bmin.x, bmin.y, bmin.z);
  box << Vec(bmax.x, bmin.y, bmin.z);
  box << Vec(bmax.x, bmax.y, bmin.z);
  box << Vec(bmin.x, bmax.y, bmin.z);

  box << Vec(bmin.x, bmin.y, bmax.z);
  box << Vec(bmax.x, bmin.y, bmax.z);
  box << Vec(bmax.x, bmax.y, bmax.z);
  box << Vec(bmin.x, bmax.y, bmax.z);

  box << Vec(bmin.x, bmax.y, bmin.z);
  box << Vec(bmax.x, bmax.y, bmin.z);
  box << Vec(bmax.x, bmax.y, bmax.z);
  box << Vec(bmin.x, bmax.y, bmax.z);

  box << Vec(bmin.x, bmin.y, bmin.z);
  box << Vec(bmax.x, bmin.y, bmin.z);
  box << Vec(bmax.x, bmin.y, bmax.z);
  box << Vec(bmin.x, bmin.y, bmax.z);  

  box << Vec(bmin.x, bmin.y, bmin.z);
  box << Vec(bmin.x, bmax.y, bmin.z);
  box << Vec(bmin.x, bmax.y, bmax.z);
  box << Vec(bmin.x, bmin.y, bmax.z);  

  box << Vec(bmax.x, bmin.y, bmin.z);
  box << Vec(bmax.x, bmax.y, bmin.z);
  box << Vec(bmax.x, bmax.y, bmax.z);
  box << Vec(bmax.x, bmin.y, bmax.z);  

  QList<Vec> boxN; 
  boxN << Vec(0,0,-1);
  boxN << Vec(0,0,-1);
  boxN << Vec(0,0,-1);
  boxN << Vec(0,0,-1);

  boxN << Vec(0,0,1);
  boxN << Vec(0,0,1);
  boxN << Vec(0,0,1);
  boxN << Vec(0,0,1);

  boxN << Vec(0,1,0);
  boxN << Vec(0,1,0);
  boxN << Vec(0,1,0);
  boxN << Vec(0,1,0);

  boxN << Vec(0,-1,0);
  boxN << Vec(0,-1,0);
  boxN << Vec(0,-1,0);
  boxN << Vec(0,-1,0);

  boxN << Vec(-1,0,0);
  boxN << Vec(-1,0,0);
  boxN << Vec(-1,0,0);
  boxN << Vec(-1,0,0);

  boxN << Vec(1,0,0);
  boxN << Vec(1,0,0);
  boxN << Vec(1,0,0);
  boxN << Vec(1,0,0);
  
  float texC[] = {1,0, 0,0, 0,1, 1,1};
  for(int i=0; i<24; i++)
    {
      m_boxData[8*i+0] = box[i].x;
      m_boxData[8*i+1] = box[i].y;
      m_boxData[8*i+2] = box[i].z;
      m_boxData[8*i+3] = boxN[i].x;
      m_boxData[8*i+4] = boxN[i].y;
      m_boxData[8*i+5] = boxN[i].z;
      m_boxData[8*i+6] = texC[2*(i%4)+0];
      m_boxData[8*i+7] = texC[2*(i%4)+1];
    }
}

void
Label::drawBox(QMatrix4x4 mvp, QVector3D vDir,
	       bool glow, bool box)
{
  glActiveTexture(GL_TEXTURE4);
  if (box)
    glBindTexture(GL_TEXTURE_2D, Global::boxSpriteTexture());
  else
    glBindTexture(GL_TEXTURE_2D, Global::circleSpriteTexture());
  glEnable(GL_TEXTURE_2D);


  glBindBuffer( GL_ARRAY_BUFFER, m_glVertBuffer);
  glBufferSubData(GL_ARRAY_BUFFER,
		  0,
		  sizeof(float)*8*24,
		  &m_boxData[0]);

  glEnable(GL_TEXTURE_2D);


  glBindVertexArray(m_glVertArray);
  glBindBuffer( GL_ARRAY_BUFFER, m_glVertBuffer);

  // Identify the components in the vertex buffer
  glEnableVertexAttribArray( 0 );
  glVertexAttribPointer( 0, //attribute 0
			 3, // size
			 GL_FLOAT, // type
			 GL_FALSE, // normalized
			 sizeof(float)*8, // stride
			 (void *)0 ); // starting offset
  
  glEnableVertexAttribArray( 1 );
  glVertexAttribPointer( 1,
			 3,
			 GL_FLOAT,
			 GL_FALSE,
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*3 );
  
  glEnableVertexAttribArray( 2 );
  glVertexAttribPointer( 2,
			 2,
			 GL_FLOAT,
			 GL_FALSE, 
			 sizeof(float)*8,
			 (char *)NULL + sizeof(float)*6 );

  glColor3f(0.5, 0.6, 1.0);

  glUseProgram(ShaderFactory::rcShader());
  GLint *rcShaderParm = ShaderFactory::rcShaderParm();
  glUniformMatrix4fv(rcShaderParm[0], 1, GL_FALSE, mvp.data() );  
  glUniform1i(rcShaderParm[1], 4); // texture

  if (glow)
    glUniform3f(rcShaderParm[2], m_hitC.x, m_hitC.y, m_hitC.z); // mix color
  else
    glUniform3f(rcShaderParm[2], m_color.x, m_color.y, m_color.z); // mix color

  if (box)
    glUniform3f(rcShaderParm[3], vDir.x(), vDir.y(), vDir.z()); // view direction
  else
    glUniform3f(rcShaderParm[3], 0,0,0); // view direction

  glUniform1f(rcShaderParm[4], 1.0); // opacity modulator
  glUniform1i(rcShaderParm[5], 5); // apply texture
  glUniform1f(rcShaderParm[6], 10); // pointsize

  if (box)
    glUniform1f(rcShaderParm[7], 0.7); // mix color
  else
    glUniform1f(rcShaderParm[7], 1); // mix color

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glIndexBuffer);  
  //glDrawElements(GL_QUADS, 24, GL_UNSIGNED_BYTE, 0);  
  
  if (box)
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, 0);  
  else // draw only bottom square
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);  

  glBindVertexArray(0);

  glUseProgram( 0 );

  glDisable(GL_TEXTURE_2D);
}
