#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <sstream>
#include <cstring>
#include <string>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

#include <Maple2/Maple2.hpp>
#include <Util/Util.hpp>

bool PackFolder(
	const fs::path& TargetFolder,
	Maple2::Identifier PackVersion
);

int main(int argc, char* argv[])
{
	std::puts(
		"MapleStory2 Filesystem packer:\n"
		"\t\"Packs\" a filesystem, creating an .m2h/.m2d pair\n"
		"\tof the original folder's name\n"
		"Build Date: " __TIMESTAMP__ "\n"
		"\t- wunkolo <wunkolo@gmail.com>\n"
		"Usage:\n\tPack <[M/N/O/P]2SF>:<Folder path>\n\tCan specify multiple paths\n"
		"Example:\n\tPack MS2F:Data/Textures O2SF:Data/Models\n"
	);
	if( argc < 2 )
	{
		std::puts("Invalid arguments");
		return EXIT_FAILURE;
	}

	for( std::size_t i = 1; i < static_cast<std::size_t>(argc); ++i )
	{
		const std::string CurArg(argv[i]);
		const std::size_t Sep = CurArg.find_first_of(":");
		if( Sep == std::string::npos )
		{
			std::puts("Invalid PackStream argument( did you forget a \':\'?)");
			continue;
		}
		const fs::path CurPath = CurArg.substr(Sep + 1);
		const std::string CurPackVersion = CurArg.substr(
			0,
			Sep
		);
		if( CurPackVersion.size() != 4 )
		{
			std::printf(
				"Invalid PackStream identifier: \'%s\'",
				CurPackVersion.c_str()
			);
		}
		const Maple2::Identifier PackVersion
			= *reinterpret_cast<const Maple2::Identifier*>(CurPackVersion.data());

		switch( PackVersion )
		{
		case Maple2::Identifier::MS2F:
		case Maple2::Identifier::NS2F:
		case Maple2::Identifier::OS2F:
		case Maple2::Identifier::PS2F:
		{
			const bool PackResult = PackFolder(CurPath, PackVersion);
			break;
		}
		default:
		{
			std::printf(
				"Unknown PackStream version: \'%s\'",
				CurPackVersion.c_str()
			);
			continue;
		}
		}
	}
	return EXIT_SUCCESS;
}

template< typename PackTraits >
bool MakePackFile(
	const fs::path& TargetFolder,
	std::ofstream& HeaderFile,
	std::ofstream& DataFile
)
{
	typename PackTraits::StreamType StreamHeader = {};
	std::vector<typename PackTraits::FileHeaderType> FileTable;
	std::ostringstream FileList;

	for(
		const auto& CurFile
		: fs::recursive_directory_iterator(
			TargetFolder,
			fs::directory_options::follow_directory_symlink
		)
	)
	{
		const auto& CurPath = CurFile.path();
		if( !fs::is_regular_file(CurPath)
			|| CurPath.extension() == ".m2h"
			|| CurPath.extension() == ".m2d"
		)
		{
			// Skip non-files
			// Skip .m2h/.m2d files that we possibly just created
			continue;
		}
		++StreamHeader.TotalFiles;
		const fs::path RelativePath = fs::path(
			CurPath.string().substr(
				TargetFolder.string().size() + 1
			)
		);

		// Create Encoded data
		std::ifstream CurFileStream(
			CurPath,
			std::ios::binary
		);
		std::string Encoded;

		typename PackTraits::FileHeaderType CurFATEntry = {};
		std::tie(
			Encoded,
			CurFATEntry.CompressedSize, // DEFLATE length
			CurFATEntry.EncodedSize // Base64 length
		) = Maple2::Util::EncryptFile(
			CurFileStream,
			PackTraits::IV_LUT,
			PackTraits::Key_LUT,
			true
		);
		CurFileStream.close();

		// Create FAT Entry
		CurFATEntry.Offset = static_cast<std::size_t>(DataFile.tellp());
		CurFATEntry.FileIndex = StreamHeader.TotalFiles;
		CurFATEntry.Compression = Maple2::CompressionType::Deflate;
		CurFATEntry.Size = fs::file_size(CurPath);
		FileTable.push_back(CurFATEntry);

		// Write Encoded data
		DataFile << Encoded;

		// Add to FileList
		FileList
		<< StreamHeader.TotalFiles
		<< ','
		<< RelativePath.string()
		<< "\r\n";
	}

	// Create encrypted filelist
	const std::string FileListData = FileList.str();
	std::puts(
		FileListData.c_str()
	);
	std::string FileListCipher;
	StreamHeader.FileListSize = FileListData.size();
	std::tie(
		FileListCipher,
		StreamHeader.FileListCompressedSize,
		// DEFLATE length
		StreamHeader.FileListEncodedSize // Base64 length
	) = Maple2::Util::EncryptString(
		FileListData,
		PackTraits::IV_LUT,
		PackTraits::Key_LUT,
		true
	);

	// Create encrypted File Allocation Table
	std::string FATString;
	std::string FATCipher;
	FATString.resize(
		FileTable.size() * sizeof(typename PackTraits::FileHeaderType)
	);
	std::memcpy(
		FATString.data(),
		FileTable.data(),
		FileTable.size() * sizeof(typename PackTraits::FileHeaderType)
	);
	StreamHeader.FATSize = FATString.size();
	std::tie(
		FATCipher,
		StreamHeader.FATCompressedSize, // DEFLATE length
		StreamHeader.FATEncodedSize // Base64 length
	) = Maple2::Util::EncryptString(
		FATString,
		PackTraits::IV_LUT,
		PackTraits::Key_LUT,
		true
	);

	// Write header
	Util::Write(HeaderFile, PackTraits::Magic);
	Util::Write(HeaderFile, StreamHeader);

	// Write File List
	HeaderFile << FileListCipher;

	// Write File Allocation Table
	HeaderFile << FATCipher;

	return true;
}

bool PackFolder(
	const fs::path& TargetFolder,
	Maple2::Identifier PackVersion
)
{
	if( !fs::exists(TargetFolder) )
	{
		std::printf(
			"Folder \"%s\" does not exist\n",
			TargetFolder.string().c_str()
		);
		return false;
	}

	if( !fs::is_directory(TargetFolder) )
	{
		std::printf(
			"\"%s\" is not a folder\n",
			TargetFolder.string().c_str()
		);
		return false;
	}

	auto TargetFile = fs::absolute(TargetFolder).parent_path() / TargetFolder.stem();

	std::ofstream HeaderFile;
	HeaderFile.open(
		TargetFile.replace_extension(".m2h"),
		std::ios::binary | std::ios::trunc
	);
	std::printf(
		"Creating header file: %s\n",
		TargetFile.string().c_str()
	);
	if( !HeaderFile.good() )
	{
		std::printf(
			"Error creating file: %s\n",
			TargetFile.string().c_str()
		);
		return false;
	}

	std::ofstream DataFile;
	DataFile.open(
		TargetFile.replace_extension(".m2d"),
		std::ios::binary | std::ios::trunc
	);
	std::printf(
		"Creating data file: %s\n",
		TargetFile.string().c_str()
	);
	if( !DataFile.good() )
	{
		std::printf(
			"Error creating file: %s\n",
			TargetFile.string().c_str()
		);
		return false;
	}

	switch( PackVersion )
	{
	case Maple2::Identifier::MS2F:
	{
		return MakePackFile<Maple2::PackTraits::MS2F>(
			TargetFolder,
			HeaderFile,
			DataFile
		);
	}
	case Maple2::Identifier::NS2F:
	{
		return MakePackFile<Maple2::PackTraits::NS2F>(
			TargetFolder,
			HeaderFile,
			DataFile
		);
	}
	case Maple2::Identifier::OS2F:
	{
		return MakePackFile<Maple2::PackTraits::OS2F>(
			TargetFolder,
			HeaderFile,
			DataFile
		);
	}
	case Maple2::Identifier::PS2F:
	{
		return MakePackFile<Maple2::PackTraits::PS2F>(
			TargetFolder,
			HeaderFile,
			DataFile
		);
	}
	}

	return false;
}
