#include "staticfunctions.h"
#include "volume.h"

#include <QtGui>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFileDialog>
#include <QApplication>
#include <QInputDialog>

#include "laszip_dll.h"

Volume::Volume() : QObject()
{
  m_zeroShift = true;

  m_coord = 0;
  m_color = 0;
  m_filenames.clear(); 

  m_scale = 1.0;
  m_shift = Vec(0,0,0);

  m_bminZ = 1;
  m_bmaxZ = 0;
  m_priority = 0;
  m_time = -1;

  m_xformPresent = false;

  //m_dpv = 3;
  //m_dpv = 4;
  m_dpv = 6;

  m_loadingNodes.clear();
  m_newLoad = false;

  m_validCamera = false;

  m_timeseries = false;
  m_ignoreScaling = false;


  m_thread = new QThread();
  m_lt = new VolumeLoaderThread();
  m_lt->moveToThread(m_thread);
  connect(this, SIGNAL(startLoading()),
	  m_lt, SLOT(startLoading()));
  m_thread->start();
}

Volume::~Volume()
{
  reset();
}

void
Volume::reset()
{
  if (m_coord) delete [] m_coord;
  if (m_color) delete [] m_color;

  m_filenames.clear();

  m_coord = 0;
  m_color = 0;

  m_loadingNodes.clear();
  m_newLoad = false;

  m_timeseries = false;
  m_ignoreScaling = false;

  m_boxSizeMin = Vec(0,0,0);
  m_boxSizeMax = Vec(0,0,0);
}

void
Volume::setCamera(Camera *cam)
{
  m_validCamera = true;

  m_camPosition = cam->position();
  m_camOrientation = cam->orientation();
  m_camSceneCenter = cam->sceneCenter();
  m_camPivot = cam->pivotPoint();
}

int
Volume::maxTime()
{
  int maxTime = 0;

  for(int d=0; d<m_trisets.count(); d++)
    maxTime = qMax(maxTime, m_trisets[d]->time());

  for(int d=0; d<m_pointClouds.count(); d++)
    //maxTime = qMax(maxTime, m_pointClouds[d]->maxTime());
    maxTime = qMax(maxTime, m_pointClouds[d]->time());

  return maxTime;
}

bool
Volume::loadOnTop(QString dirname)
{
  m_scale = 1.0;
  m_shift = Vec(0,0,0);

  if (QDir(dirname).exists("cloud.js"))
    {
      if (m_pointClouds.count() == 2)
	m_pointClouds.removeLast();

      // Load single PoTree directory
      QStringList dirnames;
      dirnames << dirname;

      PointCloud *pointCloud = new PointCloud();
      
      //--------------------------------------------
      // set defaults
      pointCloud->setShowMap(m_showMap);
      pointCloud->setGravity(m_gravity);
      pointCloud->setSkybox(m_skybox);
      pointCloud->setPlayButton(m_playbutton);
      pointCloud->setShowSphere(m_showSphere);
      pointCloud->setGroundHeight(m_groundHeight);
      pointCloud->setTeleportScale(m_teleportScale);
      pointCloud->setPointType(m_pointType);
      //--------------------------------------------

      QString name = QFileInfo(dirname).fileName();
      pointCloud->setName(name);

      m_pointClouds << pointCloud;
      pointCloud->loadMultipleTiles(dirnames);
    }
  else
    return false;
  
  postLoad(true);

  return true;
}

bool
Volume::loadDir(QString dirname)
{
  m_lt->stopLoading();

  m_filenames.clear();
  m_filenames << QDir(dirname).absolutePath();

  m_scale = 1.0;
  m_shift = Vec(0,0,0);
  m_bminZ = 1;
  m_bmaxZ = 0;
  m_xformPresent = false;
  m_priority = 0;
  m_time = -1;


  m_tiles.clear();
  m_pointClouds.clear();
  m_trisets.clear();
  
  //m_groundHeight = 0.16;
  //m_teleportScale = 1.0;
  m_groundHeight = 1.8;
  m_teleportScale = 1.0;
  m_showMap = true;
  m_gravity = false;
  m_skybox = false;
  m_playbutton = false;
  m_showSphere = false;
  m_pointType = true; // adaptive point size
  m_loadall = false;
  m_colorPresent = true;


  QStringList subdir = QDir(dirname).entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
  if (subdir.count() > 1 &&
      !QDir(dirname).exists("cloud.js"))
    {
      // if top.json exists ask whether to create one
      if (!QDir(dirname).exists("top.json"))
	{
	  QStringList items;
	  items << "Yes" << "No";
	  bool ok;
	  QString item = QInputDialog::getItem(StaticFunctions::visibleWidget(),
					       "top.json",
					       "Are you loading timeseries of point cloud ?",
					       items, 0, false, &ok);

	  if (!ok || item == "Yes")
	    {
	      // create top.json
	      QString jsonfile = QDir(dirname).absoluteFilePath("top.json");
	      QFile jfl(jsonfile);
	      jfl.open(QIODevice::WriteOnly | QIODevice::Text);
	      jfl.close();
	    }

	}

    }
    

  //---------------------------------
  if (QDir(dirname).exists("top.json"))
    {
      QString jsonfile = QDir(dirname).absoluteFilePath("top.json");
      loadTopJson(jsonfile);

      //loadAllPLY(dirname);

      // top directory contains PoTree tiles for multiple data
      QDirIterator topDirIter(dirname,
			      QDir::Dirs | QDir::Readable |
			      QDir::NoDotAndDotDot | QDir::NoSymLinks,
			      QDirIterator::NoIteratorFlags);
      int step = 0;     
      QString dirnames;
      while(topDirIter.hasNext())
	{
	  QString dirname = topDirIter.next();
	  if (!dirname.contains("StaticData"))
	    {
	      PointCloud *pointCloud = new PointCloud();
	      pointCloud->setShowMap(m_showMap);
	      pointCloud->setGravity(m_gravity);
	      pointCloud->setSkybox(m_skybox);
	      pointCloud->setPlayButton(m_playbutton);
	      pointCloud->setShowSphere(m_showSphere);
	      pointCloud->setGroundHeight(m_groundHeight);
	      pointCloud->setTeleportScale(m_teleportScale);
	      pointCloud->setPointType(m_pointType);
	      pointCloud->setColorPresent(m_colorPresent);
	      
	      m_pointClouds << pointCloud;
	      
	      if (!m_timeseries)
		step = -1;
	      
	      pointCloud->loadPoTreeMultiDir(dirname, step, m_ignoreScaling);
	      
	      step ++;

	      dirnames = dirnames + "\n" + dirname;
	    }
	}

    }
  else if (QDir(dirname).exists("cloud.js"))
    {
      // Load single PoTree directory
      QStringList dirnames;
      dirnames << dirname;

      PointCloud *pointCloud = new PointCloud();
      
      //--------------------------------------------
      // set defaults
      pointCloud->setShowMap(m_showMap);
      pointCloud->setGravity(m_gravity);
      pointCloud->setSkybox(m_skybox);
      pointCloud->setPlayButton(m_playbutton);
      pointCloud->setShowSphere(m_showSphere);
      pointCloud->setGroundHeight(m_groundHeight);
      pointCloud->setTeleportScale(m_teleportScale);
      pointCloud->setPointType(m_pointType);
      //--------------------------------------------

      QString name = QFileInfo(dirname).fileName();
      pointCloud->setName(name);

      m_pointClouds << pointCloud;


      pointCloud->loadMultipleTiles(dirnames);
    }
  else
    {
      // Load PoTree directories for individual tiles
      PointCloud *pointCloud = new PointCloud();
      m_pointClouds << pointCloud;
      pointCloud->loadPoTreeMultiDir(dirname);
    }
  //---------------------------------


//  pruneOctreeNodesBasedOnPriority();


  postLoad(true);


  //------------------------
  // start volume loader thread if loadall detected
  if (m_loadall)
    {
      m_lt->setTrisets(m_trisets);      
      m_lt->setPointClouds(m_pointClouds);      
      emit startLoading();
    }
  //------------------------

  return true;
}

bool
Volume::loadTiles(QStringList dirnames)
{
  m_lt->stopLoading();

  m_filenames = dirnames;

  m_tiles.clear();

  m_pointClouds.clear();
  PointCloud *pointCloud = new PointCloud();
  m_pointClouds << pointCloud;
  pointCloud->loadMultipleTiles(dirnames);


  postLoad(true);


  return true;
}

void
Volume::postLoad(bool showInfo)
{
  m_validCamera = false;
  
  //--------------------------------------
  // set octree node levels for each cloud
  for(int d=0; d<m_pointClouds.count(); d++)
    m_pointClouds[d]->setLevelsBelow();
  //--------------------------------------
  

  //--------------------------------------
  // collect tiles from all point clouds
  m_tiles.clear();
  for(int d=0; d<m_pointClouds.count(); d++)
    {
      m_tiles += m_pointClouds[d]->tiles();

      //-------------------------
      // required when doing manual registration
      if (d == 0)
	m_xformTileId = m_tiles.count();
      //-------------------------
    }
  //--------------------------------------
  

  //--------------------------------------
  // set same id to all nodes within a tile
  for(int d=0; d<m_tiles.count(); d++)
    {
      OctreeNode *oNode = m_tiles[d];
      oNode->setId(d);
    }
  //--------------------------------------

  
  // unload previously loaded data if any and reset all globalmin
  for(int d=0; d<m_pointClouds.count(); d++)
    {
      QList<OctreeNode*> allNodes = m_pointClouds[d]->allNodes();
      for(int od=0; od<allNodes.count(); od++)
	allNodes[od]->setGlobalMinMax(Vec(0,0,0), Vec(1,1,1));

      m_pointClouds[d]->setGlobalMinMax(Vec(0,0,0), Vec(1,1,1));
      Vec shift = m_pointClouds[d]->getShift();
      float scale = m_pointClouds[d]->getScale();
      Quaternion rot = m_pointClouds[d]->getRotation();
      Vec xformCen = m_pointClouds[d]->getXformCen();
      m_pointClouds[d]->setXform(scale, shift, rot, xformCen);
    }

  if (m_trisets.count() > 0)
    {
      m_coordMin = m_trisets[0]->bmin();
      m_coordMax = m_trisets[0]->bmax();

      for(int d=1; d<m_trisets.count(); d++)
	{
	  Vec cmin = m_trisets[d]->bmin();
	  Vec cmax = m_trisets[d]->bmax();

	  m_coordMin = StaticFunctions::minVec(cmin, m_coordMin);
	  m_coordMax = StaticFunctions::maxVec(cmax, m_coordMax);

//	  m_coordMin.x = qMin(cmin.x, m_coordMin.x);
//	  m_coordMin.y = qMin(cmin.y, m_coordMin.y);
//	  m_coordMin.z = qMin(cmin.z, m_coordMin.z);
//	  
//	  m_coordMax.x = qMax(cmax.x, m_coordMax.x);
//	  m_coordMax.y = qMax(cmax.y, m_coordMax.y);
//	  m_coordMax.z = qMax(cmax.z, m_coordMax.z);
	}
    }

  if (m_pointClouds.count() > 0)
    {
      // calculate min and max coord
      m_coordMin = m_pointClouds[0]->tightOctreeMin();
      m_coordMax = m_pointClouds[0]->tightOctreeMax();
      
      for(int d=1; d<m_pointClouds.count(); d++)
	{
	  Vec cmin = m_pointClouds[d]->tightOctreeMin();
	  Vec cmax = m_pointClouds[d]->tightOctreeMax();
	  
	  m_coordMin = StaticFunctions::minVec(cmin, m_coordMin);
	  m_coordMax = StaticFunctions::maxVec(cmax, m_coordMax);

//	  m_coordMin.x = qMin(cmin.x, m_coordMin.x);
//	  m_coordMin.y = qMin(cmin.y, m_coordMin.y);
//	  m_coordMin.z = qMin(cmin.z, m_coordMin.z);
//	  
//	  m_coordMax.x = qMax(cmax.x, m_coordMax.x);
//	  m_coordMax.y = qMax(cmax.y, m_coordMax.y);
//	  m_coordMax.z = qMax(cmax.z, m_coordMax.z);
	}
    }

  if (m_coordMin.squaredNorm() == 0 &&
      m_coordMax.squaredNorm() == 0)
    {
      m_coordMin = m_boxSizeMin;
      m_coordMax = m_boxSizeMax;
    }
  
  
  int uid = 0;
  for(int d=0; d<m_pointClouds.count(); d++)
    {
      QList<OctreeNode*> allNodes = m_pointClouds[d]->allNodes();
      for(int od=0; od<allNodes.count(); od++)
	{
	  if (m_zeroShift)
	    allNodes[od]->setGlobalMinMax(m_coordMin, m_coordMax);
	  allNodes[od]->setUId(uid);
	  uid++;
	}

      //-------------------------
      // required when doing manual registration
      if (d == 0)
	m_xformNodeId = uid;
      //-------------------------
    }

  //----------------------------
  // coordinates are shifted by m_coordMin via setGlobalMinMax
  // now lie within 0 and m_coordMax-m_coordMin
  if (m_zeroShift)
    {
      for(int d=0; d<m_trisets.count(); d++)
	m_trisets[d]->setGlobalMinMax(m_coordMin, m_coordMax);

      for(int d=0; d<m_pointClouds.count(); d++)
	m_pointClouds[d]->setGlobalMinMax(m_coordMin, m_coordMax);

      m_coordMax -= m_coordMin;
      m_coordMin = Vec(0,0,0);
    }
  //----------------------------


  qint64 totpts = 0;
  for(int d=0; d<m_tiles.count(); d++)
    {
      OctreeNode *oNode = m_tiles[d];
      int maxOct = oNode->levelsBelow();
      for(int lvl=0; lvl<=maxOct; lvl++)
	{
	  QList<OctreeNode*> onl0;
	  QList<OctreeNode*> onl1;
	  onl0 << oNode;
	  for (int i=0; i<lvl; i++)
	    {
	      onl1.clear();
	      for (int j=0; j<onl0.count(); j++)
		{
		  OctreeNode *node = onl0[j];
		  for (int k=0; k<8; k++)
		    {
		      OctreeNode *cnode = node->getChild(k);
		      if (cnode)
			onl1 << cnode;
		    }
		}
	      onl0 = onl1;
	    }
	  
	  qint64 npts = 0;
	  for (int j=0; j<onl0.count(); j++)
	    npts += onl0[j]->numpoints();
	  
	  totpts += npts;
	}
    }

  // get points from trisets
  for(int d=0; d<m_trisets.count(); d++)
    totpts += m_trisets[d]->numpoints();

  
  // maybe the points will be loaded later on
  if (totpts == 0)
    totpts = 1;
  
  
  m_npoints = totpts;


  if (!showInfo)
    return;
  
  QString mesg;

  mesg += QString("Number of points : %1\n\n").arg(m_npoints);

  mesg += QString("Min : %1 %2 %3\n")\
    .arg(m_coordMin.x)\
    .arg(m_coordMin.y)\
    .arg(m_coordMin.z);
  mesg += QString("Max : %1 %2 %3\n\n")\
    .arg(m_coordMax.x)\
    .arg(m_coordMax.y)\
    .arg(m_coordMax.z);

  int mt = maxTime();
  if (mt > 0)
    mesg += QString("MAX TIME STEP : %1\n\n").arg(mt);

  if (m_pointClouds.count() > 0)
    {
      mesg += QString("Point clouds : %1\n\n").arg(m_pointClouds.count()); 
      mesg += QString("Tile node count : %1\n").arg(m_tiles.count()); 
      mesg += QString("Octree node count : %1\n\n").arg(uid+1);
    }
  if (m_trisets.count() > 0)
    mesg += QString("Meshes : %1\n\n").arg(m_trisets.count()); 
  

  // now show this message over a visible widget
  QWidgetList wlist = QApplication::topLevelWidgets();
  for (int w=0; w<wlist.count(); w++)
    {
      if (wlist[w]->isVisible())
	{
	  QMessageBox::information(wlist[w], "Information", mesg);
	  break;
	}      
    }
}


bool
Volume::checkBoxXY(QList<OctreeNode*> topPN, Vec tmin, Vec tmax)
{
  bool maybe = false;

  for(int t=0; t<topPN.count(); t++)
    {
      for (int c=0; c<4; c++) // check for only x&y
	{
	  Vec pos((c&1)?tmin.x:tmax.x, (c&2)?tmin.y:tmax.y, (c&4)?tmin.z:tmax.z);
	  if (topPN[t]->inBoxXY(pos))
	    {
	      maybe = true;
	      break;
	    }
	}
    }

  return maybe;
}

QList<OctreeNode*>
Volume::getAllLeaves(OctreeNode* node)
{
  QList<OctreeNode*> leaves;
  leaves.clear();

  if (node->isLeaf())
    {
      leaves << node;
      return leaves;
    }

  for (int k=0; k<8; k++)
    {
      OctreeNode *cnode = node->getChild(k);
      if (cnode)
	leaves += getAllLeaves(cnode);
    }

  return leaves;
}

QList<OctreeNode*>
Volume::getNodesWithOccupiedLeaves(OctreeNode* node, QList<int> idx)
{
  QList<OctreeNode*> leaves;
  leaves.clear();

  bool ok = true;
  for (int k=0; k<idx.count(); k++)
    {
      OctreeNode *cnode = node->getChild(idx[k]);
      if (!cnode)
	ok = false;
    }

  if (ok)
    {
      leaves << node;
      return leaves;
    }

  for (int k=0; k<8; k++)
    {
      OctreeNode *cnode = node->getChild(k);
      if (cnode)
	leaves += getNodesWithOccupiedLeaves(cnode, idx);
    }

  return leaves;
}


QList<OctreeNode*>
Volume::getNodesAtLevel(OctreeNode* node, int lvl)
{
  QList<OctreeNode*> spnodes;
  spnodes.clear();

  if (node->level() == lvl)
    {
      spnodes << node;
      return spnodes;
    }

  for (int k=0; k<8; k++)
    {
      OctreeNode *cnode = node->getChild(k);
      if (cnode)
	spnodes += getNodesAtLevel(cnode, lvl);
    }

  return spnodes;
}


QList<OctreeNode*>
Volume::getNodesAtLevelBelow(OctreeNode* node, int lvl)
{
  QList<OctreeNode*> spnodes;
  spnodes.clear();

  if (node->levelsBelow() <= lvl)
    {
      spnodes << node;
      return spnodes;
    }

  for (int k=0; k<8; k++)
    {
      OctreeNode *cnode = node->getChild(k);
      if (cnode)
	spnodes += getNodesAtLevelBelow(cnode, lvl);
    }

  return spnodes;
}

int
Volume::cullNodes(OctreeNode* onode, QList<OctreeNode*> topPN)
{
  int ocd = 0;

  Vec tmin = onode->bmin();
  Vec tmax = onode->bmax(); 
      
  int all = 0;
  for (int c=0; c<4; c++) // check for only x&y 
    {
      Vec pos((c&1)?tmin.x:tmax.x, (c&2)?tmin.y:tmax.y, (c&4)?tmin.z:tmax.z);
      for(int t=0; t<topPN.count(); t++)
	{
	  if (topPN[t]->inBoxXY(pos))
	    {
	      all++;
	      break;
	    }
	} // check against higher priority cells
    }
  if (all > 2)
    {
      onode->markForDeletion();
      return 1;
    }


  // not culled
  if (onode->isLeaf())
    return 0;
  

  for (int k=0; k<8; k++)
    {
      OctreeNode *cnode = onode->getChild(k);
      if (cnode)
	ocd += cullNodes(cnode, topPN);
    }

  return ocd;

}

void
Volume::pruneOctreeNodesBasedOnPriority()
{
//  QList<OctreeNode*> allNodes = OctreeNode::allNodes();
//
//  QList<OctreeNode*> topPN;
//
//
//  QList<int> idx;
//  idx << 0 << 2 << 4 << 6;
//  for(int d=0; d<m_tiles.count(); d++)
//    {
//      if (m_tiles[d]->priority() == 1)
//	{
//	  //setLevel(m_tiles[d], 0);
//
//	  //topPN += getNodesAtLevel(m_tiles[d], 4);
//	  //topPN += getAllLeaves(m_tiles[d]);
//	  topPN += getNodesWithOccupiedLeaves(m_tiles[d], idx);
//	}
//    }
//
////  QMessageBox::information(0, "", QString("topPN : %1").arg(topPN.count()));
//  int ocd=0;
//  for(int d=0; d<m_tiles.count(); d++)
//    {
//      Vec bmin = m_tiles[d]->bmin();
//      Vec bmax = m_tiles[d]->bmax(); 
//      bool intersects = checkBoxXY(topPN, bmin, bmax);
//
//      if (m_tiles[d]->priority() == 0 && intersects)
//	{
//	  //setLevel(m_tiles[d], 0);
//	  
//	  ocd += cullNodes(m_tiles[d], topPN);
//	} // all intersecting tiles
//    }
//
////  QMessageBox::information(0, "", QString("ocd : %1").arg(ocd));
//
////  OctreeNode::pruneDeletedNodes();
}

void
Volume::loadTopJson(QString jsonfile)
{
  QFile loadFile(jsonfile);
  loadFile.open(QIODevice::ReadOnly);

  QByteArray data = loadFile.readAll();

  if (data.count() == 0) // empty file
    return;

  QJsonDocument jsonDoc(QJsonDocument::fromJson(data));

  QJsonObject jsonMod = jsonDoc.object();

  if (jsonMod.contains("top"))
    {
      QJsonObject jsonInfo = jsonMod["top"].toObject();
      
      if (jsonInfo.contains("timeseries"))
	m_timeseries = jsonInfo["timeseries"].toBool();

      if (jsonInfo.contains("ignore_scaling"))
	m_ignoreScaling = jsonInfo["ignore_scaling"].toBool();

      if (jsonInfo.contains("show_map"))
	m_showMap = jsonInfo["show_map"].toBool();

      if (jsonInfo.contains("gravity") ||
	  jsonInfo.contains("ground"))
	m_gravity = jsonInfo["gravity"].toBool();

      if (jsonInfo.contains("skybox"))
	m_skybox = jsonInfo["skybox"].toBool();

      if (jsonInfo.contains("play_button"))
	m_playbutton = jsonInfo["play_button"].toBool();

      if (jsonInfo.contains("color"))
	{
	  if (jsonInfo["color"].isBool())
	    m_colorPresent = jsonInfo["color"].toBool();
	  else
	    m_colorPresent = (jsonInfo["color"].toInt() != 0);
	}

      if (jsonInfo.contains("point_as_sphere"))
	{
	  m_showSphere = true;
          m_pointType = false;	  
	}

      if (jsonInfo.contains("loadall"))
	m_loadall = true;

      if (jsonInfo.contains("ground_height"))
	m_groundHeight = jsonInfo["ground_height"].toDouble();

      if (jsonInfo.contains("teleport_scale"))
	m_teleportScale = jsonInfo["teleport_scale"].toDouble();

      if (jsonInfo.contains("point_type"))
	{
	  QString str = jsonInfo["point_type"].toString();
	  QStringList vals = str.split(" ", QString::SkipEmptyParts);
	  if (vals[0] == "fixed")
          m_pointType = false;
	  else
	    m_pointType = true;
	}

      if (jsonInfo.contains("box_size"))
	{
	  QString str = jsonInfo["box_size"].toString();
	  QStringList vals = str.split(" ", QString::SkipEmptyParts);
	  if (vals.count() == 6)
	    {
	      float minx = vals[0].toDouble();
	      float miny = vals[1].toDouble();
	      float minz = vals[2].toDouble();
	      float maxx = vals[3].toDouble();
	      float maxy = vals[4].toDouble();
	      float maxz = vals[5].toDouble();
	      m_boxSizeMin = Vec(minx, miny, minz);
	      m_boxSizeMax = Vec(maxx, maxy, maxz);
	    }
	}
    }
}

void
Volume::loadAllPLY(QString dirname)
{
  QFileInfoList finfolist;

  QStringList namefilters;
  namefilters << "*.ply";
  namefilters << "*.vbo";

  QDirIterator dirIter(dirname,
			  namefilters,
			  QDir::Files | QDir::Readable,
			  QDirIterator::Subdirectories);

  QList<Triset*> strisets;
  QList<Triset*> dtrisets;
  
  int time = 0;
  while(dirIter.hasNext())
    {
      QString plyFile = dirIter.next();

      Triset *triset = new Triset();
      triset->setFilename(plyFile);

      if (plyFile.contains("StaticData"))
	{
	  triset->setTime(-1);
	  strisets << triset;
	}
      else
	{
	  triset->setTime(time);
	  time ++;
	  strisets << triset;
	}	  

      // if loadAll is not triggered load here itself
      if (!m_loadall)
	triset->load();
      
    }

  m_trisets << strisets;
  m_trisets << dtrisets;
}

