async function main() {
    const CWD = window.workingDir;

    return {
        mainText: "DLNA Play",
	secondaryText: 'A DLNA mediaplayer',
	imgPath: baseURL + "/fs/" + CWD + '/assets/icons/logo.png',
	onclick: async () => {
	    return {
		path: CWD + '/dlnaplay',
		cwd: CWD,
		args: ['--assets', CWD + '/assets',
		       '--fonts', CWD + '/fonts',
		       '--plugins', CWD + '/plugins',
		       '--cache', CWD + '/cache']
	    };
        }
    };
}
