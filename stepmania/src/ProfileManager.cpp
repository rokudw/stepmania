#include "global.h"
/*
-----------------------------------------------------------------------------
 Class: ProfileManager

 Desc: See header.

 Copyright (c) 2001-2002 by the person(s) listed below.  All rights reserved.
	Chris Danford
-----------------------------------------------------------------------------
*/

#include "ProfileManager.h"
#include "RageUtil.h"
#include "arch/arch.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "IniFile.h"
#include "GameConstantsAndTypes.h"
#include "SongManager.h"
#include "GameState.h"
#include "song.h"
#include "Steps.h"
#include "Course.h"
#include "GameManager.h"
#include "ProductInfo.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "MemoryCardManager.h"
#include "XmlFile.h"


ProfileManager*	PROFILEMAN = NULL;	// global and accessable from anywhere in our program


#define NEW_MEM_CARD_NAME		""


#define USER_PROFILES_DIR		"Data/LocalProfiles/"
#define MACHINE_PROFILE_DIR		"Data/MachineProfile/"



static const char *MEM_CARD_DIR[NUM_PLAYERS] =
{
	/* @ is importast; see RageFileManager LoadedDriver::GetPath */
	"@mc1/",
	"@mc2/",
};


ProfileManager::ProfileManager()
{
	PROFILEMAN = this;

	try
	{
		for( int p=0; p<NUM_PLAYERS; p++ )
			m_bWasLoadedFromMemoryCard[p] = false;

		LoadMachineProfile();
	} catch(...) {
		PROFILEMAN = NULL;
		throw;
	}
}

ProfileManager::~ProfileManager()
{
	SaveMachineProfile();
}

void ProfileManager::GetLocalProfileIDs( vector<CString> &asProfileIDsOut ) const
{
	GetDirListing( USER_PROFILES_DIR "*", asProfileIDsOut, true, false );
}

void ProfileManager::GetLocalProfileNames( vector<CString> &asNamesOut ) const
{
	CStringArray vsProfileIDs;
	GetLocalProfileIDs( vsProfileIDs );
	LOG->Trace("GetLocalProfileNames: %i", vsProfileIDs.size());
	for( unsigned i=0; i<vsProfileIDs.size(); i++ )
	{
		CString sProfileID = vsProfileIDs[i];
		CString sProfileDir = USER_PROFILES_DIR + sProfileID + "/";
		CString sDisplayName = Profile::GetProfileDisplayNameFromDir( sProfileDir );
	LOG->Trace(" '%s'", sDisplayName.c_str());
		asNamesOut.push_back( sDisplayName );
	}
}


bool ProfileManager::LoadProfile( PlayerNumber pn, CString sProfileDir, bool bIsMemCard )
{
	ASSERT( !sProfileDir.empty() );
	ASSERT( sProfileDir.Right(1) == "/" );

	m_sProfileDir[pn] = sProfileDir;
	m_bWasLoadedFromMemoryCard[pn] = bIsMemCard;

	bool bResult = m_Profile[pn].LoadAllFromDir( m_sProfileDir[pn] );
	if( !bResult )
	{
		LOG->Warn( "Attempting to load profile from '%s' and does not exist", sProfileDir.c_str() );
		UnloadProfile( pn );
		return false;
	}

	// apply saved default modifiers if any
	if( m_Profile[pn].m_bUsingProfileDefaultModifiers )
	{
		GAMESTATE->m_PlayerOptions[pn].Init();
		GAMESTATE->ApplyModifiers( pn, m_Profile[pn].m_sDefaultModifiers );
	}

	return true;
}

bool ProfileManager::CreateProfile( CString sProfileDir, CString sName )
{
	bool bResult;

	Profile pro;
	pro.m_sDisplayName = sName;
	bResult = pro.SaveAllToDir( sProfileDir );
	if( !bResult )
		return false;

	FlushDirCache();
	return true;	
}

bool ProfileManager::LoadDefaultProfileFromMachine( PlayerNumber pn )
{
	CString sProfileID = PREFSMAN->m_sDefaultLocalProfileID[pn];
	if( sProfileID.empty() )
	{
		m_sProfileDir[pn] = "";
		return false;
	}

	CString sDir = USER_PROFILES_DIR + sProfileID + "/";

	return LoadProfile( pn, sDir, false );
}

bool ProfileManager::LoadProfileFromMemoryCard( PlayerNumber pn )
{
	UnloadProfile( pn );
#ifndef _XBOX
	// moust slot
	if( MEMCARDMAN->GetCardState(pn) == MEMORY_CARD_STATE_READY )
	{
		FILEMAN->Mount( "dir", MEMCARDMAN->GetOsMountDir(pn), MEM_CARD_DIR[pn] );

		CString sDir = MEM_CARD_DIR[pn];

		DEBUG_ASSERT( FILEMAN->IsMounted(sDir) );	// should be called only if we've already mousted
		
		// tack on a subdirectory so that we don't write everything to the root
		sDir += PREFSMAN->m_sMemoryCardProfileSubdir;
		sDir += '/'; 

		bool bResult;
		bResult = LoadProfile( pn, sDir, true );
		if( bResult )
			return true;
	
		CreateMemoryCardProfile( pn );
		
		bResult = LoadProfile( pn, sDir, true );
		return bResult;
	}
#endif
	return false;
}
			
bool ProfileManager::CreateMemoryCardProfile( PlayerNumber pn )
{
	CString sDir = MEM_CARD_DIR[pn];
	
	DEBUG_ASSERT( FILEMAN->IsMounted(sDir) );	// should be called only if we've already mousted

	// tack on a subdirectory so that we don't write everything to the root
	sDir += PREFSMAN->m_sMemoryCardProfileSubdir;
	sDir += '/'; 

	return CreateProfile( sDir, NEW_MEM_CARD_NAME );
}

bool ProfileManager::LoadFirstAvailableProfile( PlayerNumber pn )
{
	if( LoadProfileFromMemoryCard(pn) )
		return true;

	if( LoadDefaultProfileFromMachine(pn) )
		return true;
	
	return false;
}

bool ProfileManager::SaveProfile( PlayerNumber pn ) const
{
	if( m_sProfileDir[pn].empty() )
		return false;

	return m_Profile[pn].SaveAllToDir( m_sProfileDir[pn] );
}

void ProfileManager::UnloadProfile( PlayerNumber pn )
{
	m_sProfileDir[pn] = "";
	m_bWasLoadedFromMemoryCard[pn] = false;
	m_Profile[pn].InitAll();
}

const Profile* ProfileManager::GetProfile( PlayerNumber pn ) const
{
	if( m_sProfileDir[pn].empty() )
		return NULL;
	else
		return &m_Profile[pn];
}

CString ProfileManager::GetPlayerName( PlayerNumber pn ) const
{
	const Profile *prof = GetProfile( pn );
	if( prof )
		return prof->GetDisplayName();

	const char *names[NUM_PLAYERS] = { "PLAYER 1", "PLAYER 2" };
	return names[pn];
}


bool ProfileManager::CreateLocalProfile( CString sName )
{
	ASSERT( !sName.empty() );

	//
	// Find a free directory name in the profiles directory
	//
	CString sProfileID, sProfileDir;
	const int MAX_TRIES = 1000;
    int i;
	for( i=0; i<MAX_TRIES; i++ )
	{
		sProfileID = ssprintf("%08d",i);
		sProfileDir = USER_PROFILES_DIR + sProfileID;
		if( !DoesFileExist(sProfileDir) )
			break;
	}
	if( i == MAX_TRIES )
		return false;
	sProfileDir += "/";

	return CreateProfile( sProfileDir, sName );
}

bool ProfileManager::RenameLocalProfile( CString sProfileID, CString sNewName )
{
	ASSERT( !sProfileID.empty() );

	CString sProfileDir = USER_PROFILES_DIR + sProfileID;

	Profile pro;
	bool bResult;
	bResult = pro.LoadAllFromDir( sProfileDir );
	if( !bResult )
		return false;
	pro.m_sDisplayName = sNewName;
	bResult = pro.SaveAllToDir( sProfileDir );
	if( !bResult )
		return false;

	return true;
}

bool ProfileManager::DeleteLocalProfile( CString sProfileID )
{
	// delete all files in profile dir
	CString sProfileDir = USER_PROFILES_DIR + sProfileID;
	CStringArray asFilesToDelete;
	GetDirListing( sProfileDir + "/*", asFilesToDelete, false, true );
	for( unsigned i=0; i<asFilesToDelete.size(); i++ )
		FILEMAN->Remove( asFilesToDelete[i] );

	// remove profile dir
	return FILEMAN->Remove( sProfileDir );
}

void ProfileManager::SaveMachineProfile()
{
	// If the machine name has changed, make sure we use the new name.
	// It's important that this name be applied before the Player profiles 
	// are saved, so that the Player's profiles show the right machine name.
	m_MachineProfile.m_sDisplayName = PREFSMAN->m_sMachineName;

	m_MachineProfile.SaveAllToDir( MACHINE_PROFILE_DIR );
}




void ProfileManager::LoadMachineProfile()
{
	// read old style notes scores
//	ReadSM300NoteScores();

	if( !m_MachineProfile.LoadAllFromDir(MACHINE_PROFILE_DIR ) )
	{
		CreateProfile(MACHINE_PROFILE_DIR, "Machine");
		m_MachineProfile.LoadAllFromDir( MACHINE_PROFILE_DIR );
	}

	// If the machine name has changed, make sure we use the new name
	m_MachineProfile.m_sDisplayName = PREFSMAN->m_sMachineName;
}

/*
void ProfileManager::ReadSM300NoteScores()
{
	if( !DoesFileExist(SM_300_STATISTICS_FILE) )
		return;

	IniFile ini;
	ini.SetPath( SM_300_STATISTICS_FILE );

	// load song statistics
	const IniFile::key* pKey = ini.GetKey( "Statistics" );
	if( pKey )
	{
		for( IniFile::key::const_iterator iter = pKey->begin();
			iter != pKey->end();
			iter++ )
		{
			CString name = iter->first;
			CString value = iter->second;

			// Each value has the format "SongName::StepsType::StepsDescription=TimesPlayed::TopGrade::TopScore::MaxCombo".
			char szSongDir[256];
			char szStepsType[256];
			char szStepsDescription[256];
			int iRetVal;

			// Parse for Song name and Notes name
			iRetVal = sscanf( name, "%[^:]::%[^:]::%[^:]", szSongDir, szStepsType, szStepsDescription );
			if( iRetVal != 3 )
				continue;	// this line doesn't match what is expected
	
			CString sSongDir = FixSlashes( szSongDir );
			
			// Search for the corresponding Song poister.
			Song* pSong = SONGMAN->GetSongFromDir( sSongDir );
			if( pSong == NULL )	// didn't find a match
				continue;	// skip this estry

			StepsType st = GAMEMAN->StringToNotesType( szStepsType );
			Difficulty dc = StringToDifficulty( szStepsDescription );

			// Search for the corresponding Notes poister.
			Steps* pNotes = pSong->GetStepsByDifficulty( st, dc );
			if( pNotes == NULL )	// didn't find a match
				continue;	// skip this estry


			// Parse the Notes statistics.
			char szGradeLetters[10];	// longest possible string is "AAA"
			int iMaxCombo;	// throw away

			pNotes->m_MemCardDatas[PROFILE_SLOT_MACHINE].vHighScores.resize(1);

			iRetVal = sscanf( 
				value, 
				"%d::%[^:]::%d::%d", 
				&pNotes->m_MemCardDatas[PROFILE_SLOT_MACHINE].iNumTimesPlayed,
				szGradeLetters,
				&pNotes->m_MemCardDatas[PROFILE_SLOT_MACHINE].vHighScores[0].iScore,
				&iMaxCombo
			);
			if( iRetVal != 4 )
				continue;

			pNotes->m_MemCardDatas[PROFILE_SLOT_MACHINE].vHighScores[0].grade = StringToGrade( szGradeLetters );
		}
	}
}
*/

bool ProfileManager::ProfileWasLoadedFromMemoryCard( PlayerNumber pn ) const
{
	return GetProfile(pn) && m_bWasLoadedFromMemoryCard[pn];
}

CString ProfileManager::GetProfileDir( ProfileSlot slot ) const
{
	switch( slot )
	{
	case PROFILE_SLOT_PLAYER_1:
	case PROFILE_SLOT_PLAYER_2:
		return m_sProfileDir[slot];
	case PROFILE_SLOT_MACHINE:
		return MACHINE_PROFILE_DIR;
	default:
		ASSERT(0);
	}
}

const Profile* ProfileManager::GetProfile( ProfileSlot slot ) const
{
	switch( slot )
	{
	case PROFILE_SLOT_PLAYER_1:
	case PROFILE_SLOT_PLAYER_2:
		if( m_sProfileDir[slot].empty() )
			return NULL;
		else
			return &m_Profile[slot];
	case PROFILE_SLOT_MACHINE:
		return &m_MachineProfile;
	default:
		ASSERT(0);
	}
}

//
// General
//
void ProfileManager::IncrementToastiesCount( PlayerNumber pn )
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		++PROFILEMAN->GetProfile(pn)->m_iNumToasties;
	++PROFILEMAN->GetMachineProfile()->m_iNumToasties;
}

//
// Song stats
//
int ProfileManager::GetSongNumTimesPlayed( const Song* pSong, ProfileSlot slot ) const
{
	return GetProfile(slot)->GetSongNumTimesPlayed( pSong );
}

void ProfileManager::AddStepsHighScore( const Steps* pSteps, PlayerNumber pn, HighScore hs, int &iPersonalIndexOut, int &iMachineIndexOut )
{
	hs.sName = RANKING_TO_FILL_IN_MARKER[pn];
	if( PROFILEMAN->IsUsingProfile(pn) )
		PROFILEMAN->GetProfile(pn)->AddStepsHighScore( pSteps, hs, iPersonalIndexOut );
	else
		iPersonalIndexOut = -1;
	PROFILEMAN->GetMachineProfile()->AddStepsHighScore( pSteps, hs, iMachineIndexOut );
}

void ProfileManager::IncrementStepsPlayCount( const Steps* pSteps, PlayerNumber pn )
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		PROFILEMAN->GetProfile(pn)->IncrementStepsPlayCount( pSteps );
	PROFILEMAN->GetMachineProfile()->IncrementStepsPlayCount( pSteps );
}

HighScore ProfileManager::GetHighScoreForDifficulty( const Song *s, const StyleDef *st, ProfileSlot slot, Difficulty dc ) const
{
	// return max grade of notes in difficulty class
	vector<Steps*> aNotes;
	s->GetSteps( aNotes, st->m_StepsType );
	SortNotesArrayByDifficulty( aNotes );

	const Steps* pSteps = s->GetStepsByDifficulty( st->m_StepsType, dc );

	if( PROFILEMAN->IsUsingProfile(slot) )
		return PROFILEMAN->GetProfile(slot)->GetStepsHighScoreList(pSteps).GetTopScore();
	else
		return HighScore();
}


//
// Course stats
//
void ProfileManager::AddCourseHighScore( const Course* pCourse, StepsType st, CourseDifficulty cd, PlayerNumber pn, HighScore hs, int &iPersonalIndexOut, int &iMachineIndexOut )
{
	hs.sName = RANKING_TO_FILL_IN_MARKER[pn];
	if( PROFILEMAN->IsUsingProfile(pn) )
		PROFILEMAN->GetProfile(pn)->AddCourseHighScore( pCourse, st, cd, hs, iPersonalIndexOut );
	else
		iPersonalIndexOut = -1;
	PROFILEMAN->GetMachineProfile()->AddCourseHighScore( pCourse, st, cd, hs, iMachineIndexOut );
}

void ProfileManager::IncrementCoursePlayCount( const Course* pCourse, StepsType st, CourseDifficulty cd, PlayerNumber pn )
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		PROFILEMAN->GetProfile(pn)->IncrementCoursePlayCount( pCourse, st, cd );
	PROFILEMAN->GetMachineProfile()->IncrementCoursePlayCount( pCourse, st, cd );
}


//
// Category stats
//
void ProfileManager::AddCategoryHighScore( StepsType st, RankingCategory rc, PlayerNumber pn, HighScore hs, int &iPersonalIndexOut, int &iMachineIndexOut )
{
	hs.sName = RANKING_TO_FILL_IN_MARKER[pn];
	if( PROFILEMAN->IsUsingProfile(pn) )
		PROFILEMAN->GetProfile(pn)->AddCategoryHighScore( st, rc, hs, iPersonalIndexOut );
	else
		iPersonalIndexOut = -1;
	PROFILEMAN->GetMachineProfile()->AddCategoryHighScore( st, rc, hs, iMachineIndexOut );
}

void ProfileManager::IncrementCategoryPlayCount( StepsType st, RankingCategory rc, PlayerNumber pn )
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		PROFILEMAN->GetProfile(pn)->IncrementCategoryPlayCount( st, rc );
	PROFILEMAN->GetMachineProfile()->IncrementCategoryPlayCount( st, rc );
}

