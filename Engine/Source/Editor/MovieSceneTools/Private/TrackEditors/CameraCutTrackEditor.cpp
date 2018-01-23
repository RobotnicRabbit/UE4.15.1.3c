// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/CameraCutTrackEditor.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Modules/ModuleManager.h"
#include "Application/ThrottleManager.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "MovieSceneCommonHelpers.h"
#include "EditorStyleSet.h"
#include "GameFramework/WorldSettings.h"
#include "LevelEditorViewport.h"
#include "Sections/CameraCutSection.h"
#include "SequencerUtilities.h"
#include "Editor.h"
#include "ActorEditorUtils.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerModule.h"
#include "TrackEditorThumbnail/TrackEditorThumbnailPool.h"

#define LOCTEXT_NAMESPACE "FCameraCutTrackEditor"


class FCameraCutTrackCommands
	: public TCommands<FCameraCutTrackCommands>
{
public:

	FCameraCutTrackCommands()
		: TCommands<FCameraCutTrackCommands>
	(
		"CameraCutTrack",
		NSLOCTEXT("Contexts", "CameraCutTrack", "CameraCutTrack"),
		NAME_None, // "MainFrame" // @todo Fix this crash
		FEditorStyle::GetStyleSetName() // Icon Style Set
	)
		, BindingCount(0)
	{ }
		
	/** Toggle the camera lock */
	TSharedPtr< FUICommandInfo > ToggleLockCamera;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;

	mutable uint32 BindingCount;
};


void FCameraCutTrackCommands::RegisterCommands()
{
	UI_COMMAND( ToggleLockCamera, "Toggle Lock Camera", "Toggle locking the viewport to the camera cut track.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::L) );
}

/* FCameraCutTrackEditor structors
 *****************************************************************************/

FCameraCutTrackEditor::FCameraCutTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer) 
{
	ThumbnailPool = MakeShareable(new FTrackEditorThumbnailPool(InSequencer));

	FCameraCutTrackCommands::Register();
}

void FCameraCutTrackEditor::OnRelease()
{
	const FCameraCutTrackCommands& Commands = FCameraCutTrackCommands::Get();
	Commands.BindingCount--;
	
	if (Commands.BindingCount < 1)
	{
		FCameraCutTrackCommands::Unregister();
	}
}

TSharedRef<ISequencerTrackEditor> FCameraCutTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FCameraCutTrackEditor(InSequencer));
}


/* ISequencerTrackEditor interface
 *****************************************************************************/

void FCameraCutTrackEditor::BindCommands(TSharedRef<FUICommandList> SequencerCommandBindings)
{
	const FCameraCutTrackCommands& Commands = FCameraCutTrackCommands::Get();

	SequencerCommandBindings->MapAction(
		Commands.ToggleLockCamera,
		FExecuteAction::CreateSP( this, &FCameraCutTrackEditor::ToggleLockCamera) );

	Commands.BindingCount++;
}

void FCameraCutTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	UMovieSceneSequence* RootMovieSceneSequence = GetSequencer()->GetRootMovieSceneSequence();

	if ((RootMovieSceneSequence == nullptr) || (RootMovieSceneSequence->GetClass()->GetName() != TEXT("LevelSequence")))
	{
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddCameraCutTrack", "Camera Cut Track"),
		LOCTEXT("AddCameraCutTooltip", "Adds a camera cut track, as well as a new camera cut at the current scrubber location if a camera is selected."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.CameraCut"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FCameraCutTrackEditor::HandleAddCameraCutTrackMenuEntryExecute),
			FCanExecuteAction::CreateRaw(this, &FCameraCutTrackEditor::HandleAddCameraCutTrackMenuEntryCanExecute)
		)
	);
}

TSharedPtr<SWidget> FCameraCutTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	// Create a container edit box
	return SNew(SHorizontalBox)

	// Add the camera combo box
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		FSequencerUtilities::MakeAddButton(LOCTEXT("CameraCutText", "Camera"), FOnGetContent::CreateSP(this, &FCameraCutTrackEditor::HandleAddCameraCutComboButtonGetMenuContent), Params.NodeIsHovered)
	]

	+ SHorizontalBox::Slot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Right)
	.AutoWidth()
	.Padding(4, 0, 0, 0)
	[
		SNew(SCheckBox)
		.IsFocusable(false)
		.IsChecked(this, &FCameraCutTrackEditor::IsCameraLocked)
		.OnCheckStateChanged(this, &FCameraCutTrackEditor::OnLockCameraClicked)
		.ToolTipText(this, &FCameraCutTrackEditor::GetLockCameraToolTip)
		.ForegroundColor(FLinearColor::White)
		.CheckedImage(FEditorStyle::GetBrush("Sequencer.LockCamera"))
		.CheckedHoveredImage(FEditorStyle::GetBrush("Sequencer.LockCamera"))
		.CheckedPressedImage(FEditorStyle::GetBrush("Sequencer.LockCamera"))
		.UncheckedImage(FEditorStyle::GetBrush("Sequencer.UnlockCamera"))
		.UncheckedHoveredImage(FEditorStyle::GetBrush("Sequencer.UnlockCamera"))
		.UncheckedPressedImage(FEditorStyle::GetBrush("Sequencer.UnlockCamera"))
	];
}


TSharedRef<ISequencerSection> FCameraCutTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check(SupportsType(SectionObject.GetOuter()->GetClass()));

	return MakeShareable(new FCameraCutSection(GetSequencer(), ThumbnailPool, SectionObject));
}


bool FCameraCutTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return (Type == UMovieSceneCameraCutTrack::StaticClass());
}


void FCameraCutTrackEditor::Tick(float DeltaTime)
{
	TSharedPtr<ISequencer> SequencerPin = GetSequencer();
	if (!SequencerPin.IsValid())
	{
		return;
	}

	EMovieScenePlayerStatus::Type PlaybackState = SequencerPin->GetPlaybackStatus();

	if (FSlateThrottleManager::Get().IsAllowingExpensiveTasks() && PlaybackState != EMovieScenePlayerStatus::Playing && PlaybackState != EMovieScenePlayerStatus::Scrubbing)
	{
		SequencerPin->EnterSilentMode();

		float SavedTime = SequencerPin->GetLocalTime();

		if (DeltaTime > 0.f && ThumbnailPool->DrawThumbnails())
		{
			SequencerPin->SetLocalTimeDirectly(SavedTime);
		}

		SequencerPin->ExitSilentMode();
	}
}


const FSlateBrush* FCameraCutTrackEditor::GetIconBrush() const
{
	return FEditorStyle::GetBrush("Sequencer.Tracks.CameraCut");
}


/* FCameraCutTrackEditor implementation
 *****************************************************************************/

bool FCameraCutTrackEditor::AddKeyInternal( float KeyTime, const FGuid ObjectGuid )
{
	UMovieSceneCameraCutTrack* CameraCutTrack = FindOrCreateCameraCutTrack();
	const TArray<UMovieSceneSection*>& AllSections = CameraCutTrack->GetAllSections();

	CameraCutTrack->AddNewCameraCut(ObjectGuid, KeyTime);

	return true;
}


UMovieSceneCameraCutTrack* FCameraCutTrackEditor::FindOrCreateCameraCutTrack()
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
	UMovieSceneTrack* CameraCutTrack = FocusedMovieScene->GetCameraCutTrack();

	if (CameraCutTrack == nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddCameraCutTrack_Transaction", "Add Camera Cut Track"));
		FocusedMovieScene->Modify();
		
		CameraCutTrack = FocusedMovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
	}

	return CastChecked<UMovieSceneCameraCutTrack>(CameraCutTrack);
}


/* FCameraCutTrackEditor callbacks
 *****************************************************************************/

bool FCameraCutTrackEditor::HandleAddCameraCutTrackMenuEntryCanExecute() const
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	return ((FocusedMovieScene != nullptr) && (FocusedMovieScene->GetCameraCutTrack() == nullptr));
}


void FCameraCutTrackEditor::HandleAddCameraCutTrackMenuEntryExecute()
{
	FindOrCreateCameraCutTrack();
	GetSequencer()->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
}

bool FCameraCutTrackEditor::IsCameraPickable(const AActor* const PickableActor)
{
	if (PickableActor->IsListedInSceneOutliner() &&
		!FActorEditorUtils::IsABuilderBrush(PickableActor) &&
		!PickableActor->IsA( AWorldSettings::StaticClass() ) &&
		!PickableActor->IsPendingKill())
	{	
		UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromActor(PickableActor);
		if (CameraComponent)	
		{
			return true;
		}
	}
	return false;
}

TSharedRef<SWidget> FCameraCutTrackEditor::HandleAddCameraCutComboButtonGetMenuContent()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	using namespace SceneOutliner;

	SceneOutliner::FInitializationOptions InitOptions;
	{
		InitOptions.Mode = ESceneOutlinerMode::ActorPicker;
		InitOptions.bShowHeaderRow = false;
		InitOptions.bFocusSearchBoxWhenOpened = true;
		InitOptions.bShowTransient = true;
		InitOptions.bShowCreateNewFolder = false;
		// Only want the actor label column
		InitOptions.ColumnMap.Add(FBuiltInColumnTypes::Label(), FColumnInfo(EColumnVisibility::Visible, 0));

		// Only display Actors that we can attach too
		InitOptions.Filters->AddFilterPredicate( SceneOutliner::FActorFilterPredicate::CreateSP(this, &FCameraCutTrackEditor::IsCameraPickable) );
	}		

	// Actor selector to allow the user to choose a parent actor
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>( "SceneOutliner" );

	TSharedRef< SWidget > MenuWidget = 
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.MaxDesiredHeight(400.0f)
			.WidthOverride(300.0f)
			[
				SceneOutlinerModule.CreateSceneOutliner(
					InitOptions,
					FOnActorPicked::CreateSP(this, &FCameraCutTrackEditor::HandleAddCameraCutComboButtonMenuEntryExecute )
					)
			]
		];

	MenuBuilder.AddWidget(MenuWidget, FText::GetEmpty(), false);
	return MenuBuilder.MakeWidget();
}


void FCameraCutTrackEditor::HandleAddCameraCutComboButtonMenuEntryExecute(AActor* Camera)
{
	FGuid ObjectGuid = FindOrCreateHandleToObject(Camera).Handle;

	if (ObjectGuid.IsValid())
	{
		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FCameraCutTrackEditor::AddKeyInternal, ObjectGuid));
	}
}

ECheckBoxState FCameraCutTrackEditor::IsCameraLocked() const
{
	if (GetSequencer()->IsPerspectiveViewportCameraCutEnabled())
	{
		return ECheckBoxState::Checked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}


void FCameraCutTrackEditor::OnLockCameraClicked(ECheckBoxState CheckBoxState)
{
	if (CheckBoxState == ECheckBoxState::Checked)
	{
		for (int32 i = 0; i < GEditor->LevelViewportClients.Num(); ++i)
		{		
			FLevelEditorViewportClient* LevelVC = GEditor->LevelViewportClients[i];
			if (LevelVC && LevelVC->IsPerspective() && LevelVC->AllowsCinematicPreview() && LevelVC->GetViewMode() != VMI_Unknown)
			{
				LevelVC->SetActorLock(nullptr);
				LevelVC->bLockedCameraView = false;
				LevelVC->UpdateViewForLockedActor();
				LevelVC->Invalidate();
			}
		}
		GetSequencer()->SetPerspectiveViewportCameraCutEnabled(true);
	}
	else
	{
		GetSequencer()->UpdateCameraCut(nullptr, nullptr);
		GetSequencer()->SetPerspectiveViewportCameraCutEnabled(false);
	}

	GetSequencer()->ForceEvaluate();
}

void FCameraCutTrackEditor::ToggleLockCamera()
{
	OnLockCameraClicked(IsCameraLocked() == ECheckBoxState::Checked ?  ECheckBoxState::Unchecked :  ECheckBoxState::Checked);
}

FText FCameraCutTrackEditor::GetLockCameraToolTip() const
{
	return IsCameraLocked() == ECheckBoxState::Checked ?
		LOCTEXT("UnlockCamera", "Unlock Viewport from Camera Cuts") :
		LOCTEXT("LockCamera", "Lock Viewport to Camera Cuts");
}

#undef LOCTEXT_NAMESPACE